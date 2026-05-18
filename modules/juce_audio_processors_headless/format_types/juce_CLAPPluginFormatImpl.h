/* SPDX-License-Identifier: ISC
 *
 * juce_CLAPPluginFormatImpl.h — internal CLAP host plumbing.
 *
 * This header is included from juce_CLAPPluginFormatHeadless.cpp inside
 * `namespace juce { }`.  System headers MUST NOT be included here for
 * the same nested-namespace reason documented in juce_winelib_path.h.
 *
 * Contents at Phase C:
 *   - CLAPModule          : ref-counted PE handle (one per .clap file),
 *                           manages clap_entry init/deinit lifecycle and
 *                           exposes the plugin factory.
 *   - getOrCreateModule() : cache lookup + creation.
 *   - makeDescriptionFromClapDescriptor() :
 *                           clap_plugin_descriptor_t → juce::PluginDescription.
 *   - findDescriptionsForFile() :
 *                           open module, iterate factory, return descriptions.
 *
 * Phase D will add CLAPPluginInstance to this header.
 *
 * RT-safety: only the load + factory-enumerate path is implemented here.
 * No audio-thread code in Phase C.
 *
 * Per CLAP 1.2.0 spec (entry.h:34-60), init() / deinit() may be called
 * multiple times in matched pairs.  We honour this by ref-counting on
 * the JUCE side: first reference triggers init(), last reference
 * triggers deinit() + FreeLibrary().
 */

#pragma once

#include "juce_winelib_path.h"
#include "juce_winelib_dispatch.h"

// NB: clap headers must be included by the TU BEFORE namespace juce { }
// opens, so the clap_* types end up at global namespace and not
// juce::clap_*.  See juce_winelib_path.h's nested-namespace warning for
// the same trap.  This impl header is included from inside `namespace
// juce { }`; clap_winelib_abi.h is included from juce_CLAPPluginFormatHeadless.cpp
// at TU top-level.


//==============================================================================
// One-time cache of CLAPModule pointers keyed by File.  Mirrors the VST3
// RefCountedDllHandle pattern.  Cache holds raw pointers; ownership is
// via ReferenceCountedObjectPtr — destructor removes from cache.

class CLAPModule;

static CriticalSection& getCLAPModuleCacheLock()
{
    static CriticalSection lk;
    return lk;
}

static Array<CLAPModule*>& getCLAPModuleCache()
{
    static Array<CLAPModule*> cache;
    return cache;
}


//==============================================================================
// CLAPModule — owns a PE handle + manages the clap_entry init/deinit
// lifecycle.  Cached by file path so multiple plugin instances from the
// same .clap share one PE load.

class CLAPModule final : public ReferenceCountedObject
{
public:
    using Ptr = ReferenceCountedObjectPtr<CLAPModule>;

    ~CLAPModule() override
    {
        {
            const ScopedLock sl (getCLAPModuleCacheLock());
            getCLAPModuleCache().removeFirstMatchingValue (this);
        }

        if (initialised && entry != nullptr && entry->deinit != nullptr)
        {
            globalWineDispatcher().run ([&]
            {
                entry->deinit();
            });
            winelib_log ("[CLAPModule] deinit done '%s'", file.getFullPathName().toRawUTF8());
        }

        if (peHandle != nullptr)
        {
            globalWineDispatcher().run ([&]
            {
                FreeLibrary (peHandle);
            });
            peHandle = nullptr;
        }
    }

    static Ptr getOrCreate (const File& f)
    {
        const ScopedLock sl (getCLAPModuleCacheLock());

        for (auto* existing : getCLAPModuleCache())
            if (existing->file == f)
                return Ptr (existing);

        Ptr fresh = new CLAPModule (f);

        if (! fresh->isReady())
            return nullptr;

        getCLAPModuleCache().add (fresh.get());
        return fresh;
    }

    const clap_plugin_entry_t*   getEntry()   const noexcept { return entry; }
    const clap_plugin_factory_t* getFactory() const noexcept { return factory; }
    File                         getFile()    const noexcept { return file; }
    String                       getDosPath() const noexcept { return dosPath; }

    bool isReady() const noexcept { return peHandle != nullptr && entry != nullptr && factory != nullptr && initialised; }

private:
    explicit CLAPModule (const File& f)
        : file (f)
    {
        open();
    }

    void open()
    {
        if (! winelib_is_pe_file (file))
        {
            winelib_log ("[CLAPModule] '%s' is not a PE file — rejecting",
                         file.getFullPathName().toRawUTF8());
            return;
        }

        dosPath = winelib_unix_to_dos_path (file.getFullPathName());
        winelib_log ("[CLAPModule] DOS path '%s'", dosPath.toRawUTF8());

        auto widePath = winelib_path_to_utf16 (dosPath);

        // LoadLibraryW must run on a Win32 thread so the plugin's TEB /
        // TLS / COM-apartment state initializes in a fully-formed thread
        // context.  Same rationale as the VST3 path in
        // juce_VST3PluginFormatImpl.h:1224-1235.
        globalWineDispatcher().run ([&]
        {
            peHandle = LoadLibraryW (widePath.getData());
        });

        winelib_log ("[CLAPModule] LoadLibraryW -> %p", peHandle);

        if (peHandle == nullptr)
            return;

        // clap_entry is a DATA symbol (extern const clap_plugin_entry_t),
        // not a function — GetProcAddress returns its address directly.
        entry = reinterpret_cast<const clap_plugin_entry_t*> (
                    GetProcAddress (peHandle, "clap_entry"));

        winelib_log ("[CLAPModule] clap_entry @ %p", (const void*) entry);

        if (entry == nullptr)
        {
            globalWineDispatcher().run ([&] { FreeLibrary (peHandle); });
            peHandle = nullptr;
            return;
        }

        if (! clap_version_is_compatible (entry->clap_version))
        {
            winelib_log ("[CLAPModule] incompatible CLAP version %u.%u.%u",
                         entry->clap_version.major,
                         entry->clap_version.minor,
                         entry->clap_version.revision);
            globalWineDispatcher().run ([&] { FreeLibrary (peHandle); });
            peHandle = nullptr;
            entry = nullptr;
            return;
        }

        // CLAP plugins receive the library path in init().  Pass the
        // Wine DOS form so any plugin that manipulates the path sees a
        // normal Windows path rather than the raw Linux unix path.
        const auto dosUtf8 = dosPath.toRawUTF8();
        bool initOk = false;

        globalWineDispatcher().run ([&]
        {
            initOk = entry->init (dosUtf8);
        });

        winelib_log ("[CLAPModule] init('%s') -> %d", dosUtf8, (int) initOk);

        if (! initOk)
        {
            // Per CLAP spec: if init() returns false we must NOT call
            // deinit().  Mark not-initialised so dtor skips deinit too.
            globalWineDispatcher().run ([&] { FreeLibrary (peHandle); });
            peHandle = nullptr;
            entry = nullptr;
            return;
        }

        initialised = true;

        const void* rawFactory = nullptr;
        globalWineDispatcher().run ([&]
        {
            rawFactory = entry->get_factory (CLAP_PLUGIN_FACTORY_ID);
        });

        factory = static_cast<const clap_plugin_factory_t*> (rawFactory);
        winelib_log ("[CLAPModule] factory @ %p", (const void*) factory);

        if (factory == nullptr)
        {
            // Initialised but no plugin factory — not a usable CLAP host
            // target.  Tear down init so deinit is matched.
            globalWineDispatcher().run ([&]
            {
                if (entry != nullptr && entry->deinit != nullptr)
                    entry->deinit();
                FreeLibrary (peHandle);
            });
            peHandle = nullptr;
            entry = nullptr;
            initialised = false;
        }
    }

    File file;
    String dosPath;
    void* peHandle = nullptr;
    const clap_plugin_entry_t*   entry   = nullptr;
    const clap_plugin_factory_t* factory = nullptr;
    bool initialised = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CLAPModule)
};


//==============================================================================
// Map CLAP plugin descriptor features → JUCE PluginDescription category.

static String clapCategoryFromFeatures (const char* const* features)
{
    if (features == nullptr)
        return {};

    // CLAP feature strings (plugin-features.h) we map to JUCE-friendly
    // category names.  Order matters: first match wins, preferring more-
    // specific over less-specific.
    for (const char* const* p = features; *p != nullptr; ++p)
    {
        const String f (*p);

        if (f == CLAP_PLUGIN_FEATURE_INSTRUMENT)          return "Instrument";
        if (f == CLAP_PLUGIN_FEATURE_SYNTHESIZER)         return "Synth";
        if (f == CLAP_PLUGIN_FEATURE_SAMPLER)             return "Sampler";
        if (f == CLAP_PLUGIN_FEATURE_DRUM)                return "Drum";
        if (f == CLAP_PLUGIN_FEATURE_DRUM_MACHINE)        return "Drum Machine";
        if (f == CLAP_PLUGIN_FEATURE_NOTE_EFFECT)         return "MIDI Effect";
        if (f == CLAP_PLUGIN_FEATURE_NOTE_DETECTOR)       return "Note Detector";
        if (f == CLAP_PLUGIN_FEATURE_ANALYZER)            return "Analyzer";
    }

    // Second pass for generic categories (only if no specific match).
    for (const char* const* p = features; *p != nullptr; ++p)
    {
        const String f (*p);

        if (f == CLAP_PLUGIN_FEATURE_AUDIO_EFFECT) return "Effect";
    }

    return {};
}

static PluginDescription makeDescriptionFromClapDescriptor (const clap_plugin_descriptor_t& d,
                                                            const File& file)
{
    PluginDescription desc;

    desc.pluginFormatName  = "CLAP";
    desc.fileOrIdentifier  = file.getFullPathName();
    desc.lastFileModTime   = file.getLastModificationTime();
    desc.lastInfoUpdateTime = Time::getCurrentTime();

    desc.name             = String::fromUTF8 (d.name);
    desc.descriptiveName  = desc.name;
    desc.manufacturerName = String::fromUTF8 (d.vendor);
    desc.version          = String::fromUTF8 (d.version);
    desc.category         = clapCategoryFromFeatures (d.features);

    // CLAP plugin id is a stable reverse-DNS string.
    // Store its hash as uniqueId so JUCE's KnownPluginList can compare
    // instances of the same plugin found at different paths.  Keep the
    // human id available in fileOrIdentifier-adjacent metadata if we
    // need it for create_plugin lookups (Phase D will store it on the
    // instance side).
    if (d.id != nullptr)
    {
        const String idStr (d.id);
        desc.uniqueId   = (int) idStr.hashCode();
        desc.deprecatedUid = desc.uniqueId;
    }

    // Detect plugin type from features: instrument vs effect drives
    // hasSharedContainer / isInstrument heuristics in JUCE.
    if (d.features != nullptr)
    {
        for (const char* const* p = d.features; *p != nullptr; ++p)
        {
            const String f (*p);
            if (f == CLAP_PLUGIN_FEATURE_INSTRUMENT
                || f == CLAP_PLUGIN_FEATURE_SYNTHESIZER
                || f == CLAP_PLUGIN_FEATURE_SAMPLER
                || f == CLAP_PLUGIN_FEATURE_DRUM
                || f == CLAP_PLUGIN_FEATURE_DRUM_MACHINE)
            {
                desc.isInstrument = true;
                break;
            }
        }
    }

    // Phase D will fill in audio I/O counts via the audio-ports extension.
    // For now leave at JUCE defaults; KnownPluginList re-scans these on
    // first instantiation if zero.
    desc.numInputChannels  = desc.isInstrument ? 0 : 2;
    desc.numOutputChannels = 2;

    return desc;
}


//==============================================================================
// Open the module, iterate its factory, produce PluginDescriptions.
// Helper used by CLAPPluginFormatHeadless::findAllTypesForFile.

static void findDescriptionsForFile (OwnedArray<PluginDescription>& out, const File& file)
{
    auto module = CLAPModule::getOrCreate (file);

    if (module == nullptr)
    {
        winelib_log ("[CLAP scan] module load failed '%s'",
                     file.getFullPathName().toRawUTF8());
        return;
    }

    const auto* factory = module->getFactory();
    if (factory == nullptr || factory->get_plugin_count == nullptr)
        return;

    uint32_t count = 0;
    globalWineDispatcher().run ([&]
    {
        count = factory->get_plugin_count (factory);
    });

    winelib_log ("[CLAP scan] %s : %u plugins", file.getFileName().toRawUTF8(), count);

    for (uint32_t i = 0; i < count; ++i)
    {
        const clap_plugin_descriptor_t* descPtr = nullptr;
        globalWineDispatcher().run ([&]
        {
            descPtr = factory->get_plugin_descriptor (factory, i);
        });

        if (descPtr == nullptr || descPtr->id == nullptr || descPtr->name == nullptr)
        {
            winelib_log ("[CLAP scan]   index %u: null/incomplete descriptor", i);
            continue;
        }

        winelib_log ("[CLAP scan]   index %u: id='%s' name='%s' vendor='%s'",
                     i,
                     descPtr->id,
                     descPtr->name,
                     descPtr->vendor != nullptr ? descPtr->vendor : "");

        out.add (new PluginDescription (makeDescriptionFromClapDescriptor (*descPtr, file)));
    }
}


//==============================================================================
// Per-thread dispatch role.  CLAP's clap_host_thread_check extension needs
// to answer "am I on the main thread / audio thread" — but our winelib
// model marshals BOTH main-thread and audio-thread calls onto the same
// per-instance Win32 dispatcher worker (for Win32 thread-identity stability,
// see VST3 Phase 3 lessons).  From the plugin's perspective, when it calls
// init()/activate() it expects host_is_main_thread()==true, and when it
// calls process() it expects host_is_audio_thread()==true.  Since both run
// on the same worker thread, the answer depends on *which call site* the
// worker is currently serving, not on thread identity.
//
// We track that with a thread_local "what role is this thread currently
// playing".  RAII helper sets it to Main/Audio for the duration of a
// dispatched call.  Plugin's introspection via host_is_main/audio_thread
// then matches what CLAP spec expects.

enum class ClapDispatchRole { Other, Main, Audio };

inline thread_local ClapDispatchRole clap_current_role = ClapDispatchRole::Other;

struct ScopedClapDispatchRole
{
    explicit ScopedClapDispatchRole (ClapDispatchRole r) noexcept
        : previous (clap_current_role) { clap_current_role = r; }
    ~ScopedClapDispatchRole() noexcept { clap_current_role = previous; }
    ClapDispatchRole previous;
};


//==============================================================================
// JUCE MidiBuffer ↔ clap_input_events_t bridge.  Per-instance ring of
// pre-formatted clap_event_note / clap_event_midi entries; populated each
// processBlock from the incoming MidiBuffer.  RT-safe: zero allocation on
// the audio thread (storage is HeapBlock-backed, sized once at activate).

struct ClapEventBridge
{
    // Largest single event we emit (note + midi are both <= clap_event_note_t
    // in size on x86_64; reserve clap_event_note_t-sized slots).
    static constexpr size_t kSlotBytes = sizeof (clap_event_note_t);

    // Pre-allocated event storage + per-event pointers.  Cap chosen to
    // comfortably hold a dense MIDI block (mod-wheel CC sweep + chord)
    // without ever reallocating on the audio thread.
    static constexpr uint32_t kMaxEventsPerBlock = 1024;

    HeapBlock<uint8_t>                    storage;          // kMaxEventsPerBlock * kSlotBytes
    HeapBlock<const clap_event_header_t*> ptrs;             // kMaxEventsPerBlock entries
    uint32_t                              count = 0;

    void ensureCapacity()
    {
        if (storage.getData() == nullptr) storage.calloc (kMaxEventsPerBlock * kSlotBytes);
        if (ptrs.getData() == nullptr)    ptrs   .calloc (kMaxEventsPerBlock);
    }

    void clear() noexcept { count = 0; }

    // Translate a JUCE MidiMessage into one CLAP event entry.  Drops the
    // event if we're at capacity (RT-safe: no allocation).
    void append (const MidiMessage& m, int sampleOffset) noexcept
    {
        if (count >= kMaxEventsPerBlock) return;

        auto* slot = storage.getData() + count * kSlotBytes;

        if (m.isNoteOn() || m.isNoteOff())
        {
            auto* ev = reinterpret_cast<clap_event_note_t*> (slot);
            ev->header.size     = (uint32_t) sizeof (clap_event_note_t);
            ev->header.time     = (uint32_t) jmax (0, sampleOffset);
            ev->header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            ev->header.type     = (uint16_t) (m.isNoteOn() ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF);
            ev->header.flags    = CLAP_EVENT_IS_LIVE;
            ev->note_id    = -1;
            ev->port_index = 0;
            ev->channel    = (int16_t) (m.getChannel() - 1);  // JUCE 1..16 → CLAP 0..15
            ev->key        = (int16_t) m.getNoteNumber();
            ev->velocity   = m.getFloatVelocity();
            ptrs[count++]  = &ev->header;
            return;
        }

        // Everything else: raw MIDI passthrough (CC, pitch bend, aftertouch,
        // program change, etc).  Plugin's clap.note-ports must include a
        // CLAP_NOTE_DIALECT_MIDI port to consume these — most synths do.
        if (m.getRawDataSize() >= 1 && m.getRawDataSize() <= 3)
        {
            auto* ev = reinterpret_cast<clap_event_midi_t*> (slot);
            ev->header.size     = (uint32_t) sizeof (clap_event_midi_t);
            ev->header.time     = (uint32_t) jmax (0, sampleOffset);
            ev->header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            ev->header.type     = (uint16_t) CLAP_EVENT_MIDI;
            ev->header.flags    = CLAP_EVENT_IS_LIVE;
            ev->port_index = 0;
            const auto* raw = m.getRawData();
            ev->data[0] = (uint8_t) raw[0];
            ev->data[1] = (uint8_t) (m.getRawDataSize() > 1 ? raw[1] : 0);
            ev->data[2] = (uint8_t) (m.getRawDataSize() > 2 ? raw[2] : 0);
            ptrs[count++] = &ev->header;
        }
        // SysEx / longer messages: dropped for now (clap_event_midi_sysex
        // needs separate buffer management — Phase F if needed).
    }
};


// clap_input_events_t vtable backed by a ClapEventBridge in ctx.

static uint32_t CLAP_ABI inputEventsSize (const clap_input_events_t* list)
{
    return static_cast<const ClapEventBridge*> (list->ctx)->count;
}

static const clap_event_header_t* CLAP_ABI inputEventsGet (const clap_input_events_t* list, uint32_t index)
{
    const auto* b = static_cast<const ClapEventBridge*> (list->ctx);
    if (index >= b->count) return nullptr;
    return b->ptrs[index];
}

// Output events: accept but discard for now.  Phase F will route MIDI/param
// output back to JUCE's MidiBuffer + parameter listeners.
static bool CLAP_ABI outputEventsTryPush (const clap_output_events_t*, const clap_event_header_t*)
{
    return true;
}


//==============================================================================
// CLAPPluginInstance — Phase D: silent-passthrough lifecycle.
//
// Lifecycle (CLAP spec threading annotations in brackets):
//   create_plugin                    [thread-safe — but plugin not yet usable]
//   init                             [main-thread]
//   activate                         [main-thread & !active]
//   start_processing → process → stop_processing  [audio-thread & active]
//   deactivate                       [main-thread & active]
//   destroy                          [main-thread & !active]
//
// All [main-thread] calls marshal through this instance's
// WineWin32Dispatcher so the plugin sees a stable Win32 thread identity
// for its TEB/TLS/COM apartment.  Audio-thread calls run inline on
// JUCE's audio thread (a pthread) — by the time we get there the plugin
// has been fully initialised on a Win32 thread, so TLS lookups inside
// process() navigate an already-formed state.  Same trade we make for
// VST3 winelib.
//
// Phase D scope: no CLAP extensions other than audio-ports (queried
// minimally to size buffer arrays).  processBlock builds the
// clap_process_t per call, hands JUCE channel pointers straight through
// to plugin->process(), zeros unused tail in the output buffer.

class CLAPPluginInstance : public AudioPluginInstance
{
public:
    // Templated factory so the non-headless GUI subclass can construct
    // itself through the same setup chain (mirrors VST3PluginInstance's
    // relation to VST3PluginInstanceHeadless).
    template <typename InstanceT = CLAPPluginInstance>
    static std::unique_ptr<InstanceT> create (CLAPModule::Ptr module,
                                              const PluginDescription& desc,
                                              const String& clapPluginId)
    {
        if (module == nullptr || module->getFactory() == nullptr)
            return nullptr;

        std::unique_ptr<InstanceT> inst (new InstanceT (module, desc, clapPluginId));

        if (! inst->createAndInitialise())
            return nullptr;

        return inst;
    }

    ~CLAPPluginInstance() override
    {
        teardown();
    }

    //==============================================================================
    // AudioPluginInstance / AudioProcessor overrides

    const String getName() const override                  { return cachedDescription.name; }

    void fillInPluginDescription (PluginDescription& description) const override
    {
        description = cachedDescription;
    }

    void prepareToPlay (double sampleRate, int blockSize) override
    {
        winelib_log ("[CLAPInstance] prepareToPlay sr=%g block=%d for '%s'",
                     sampleRate, blockSize, cachedDescription.name.toRawUTF8());

        currentSampleRate = sampleRate;
        currentBlockSize  = blockSize;

        setRateAndBufferSizeDetails (sampleRate, blockSize);

        if (plugin == nullptr)
            return;

        // Stop processing first if we're switching parameters under an
        // already-running plugin.
        if (processing.load (std::memory_order_acquire))
        {
            instanceDispatcher.run ([&]
            {
                ScopedClapDispatchRole role (ClapDispatchRole::Audio);
                if (plugin->stop_processing != nullptr)
                    plugin->stop_processing (plugin);
            });
            processing.store (false, std::memory_order_release);
        }

        if (activated.load (std::memory_order_acquire))
        {
            instanceDispatcher.run ([&]
            {
                ScopedClapDispatchRole role (ClapDispatchRole::Main);
                plugin->deactivate (plugin);
            });
            activated.store (false, std::memory_order_release);
        }

        winelib_log ("[CLAPInstance] activate start");
        bool ok = false;
        instanceDispatcher.run ([&]
        {
            ScopedClapDispatchRole role (ClapDispatchRole::Main);
            ok = plugin->activate (plugin,
                                   sampleRate,
                                   (uint32_t) jmax (1, blockSize),
                                   (uint32_t) jmax (1, blockSize));
        });
        winelib_log ("[CLAPInstance] activate -> %d", (int) ok);

        if (! ok)
        {
            winelib_log ("[CLAP] activate(%g, %d) failed for '%s'",
                         sampleRate, blockSize, cachedDescription.name.toRawUTF8());
            return;
        }

        activated.store (true, std::memory_order_release);

        // start_processing is an audio-thread call per spec, but JUCE
        // doesn't guarantee prepareToPlay is on the audio thread.  Other
        // CLAP hosts commonly defer start_processing to first processBlock
        // call — we do the same.
    }

    void releaseResources() override
    {
        if (plugin == nullptr)
            return;

        if (processing.load (std::memory_order_acquire))
        {
            instanceDispatcher.run ([&]
            {
                ScopedClapDispatchRole role (ClapDispatchRole::Audio);
                if (plugin->stop_processing != nullptr)
                    plugin->stop_processing (plugin);
            });
            processing.store (false, std::memory_order_release);
        }

        if (activated.load (std::memory_order_acquire))
        {
            instanceDispatcher.run ([&]
            {
                ScopedClapDispatchRole role (ClapDispatchRole::Main);
                plugin->deactivate (plugin);
            });
            activated.store (false, std::memory_order_release);
        }
    }

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer& midiMessages) override
    {
        if (plugin == nullptr || ! activated.load (std::memory_order_acquire))
        {
            buffer.clear();
            return;
        }

        // Capture audio thread id on first invocation so clap_host_thread_check
        // can answer plugin queries.  std::thread::id default-constructed is
        // the sentinel; once set it's stable for the instance's life.
        if (audioThreadId.load (std::memory_order_acquire) == std::thread::id())
            audioThreadId.store (std::this_thread::get_id(), std::memory_order_release);

        // start_processing on first audio-thread invocation post-activate.
        // (Plugin lifecycle: must call start before the first process.)
        // Marshal through instanceDispatcher — same Win32-thread-identity
        // requirement as VST3's audio path: PE plugins commonly tie
        // CRITICAL_SECTION owner state and TLS lookups to thread identity;
        // calling process() from JUCE's audio pthread races with state set
        // up on the dispatcher thread during activate() and ends in
        // ExitProcess().  PI-aware PiMutex inside the dispatcher means
        // audio-thread priority gets inherited by the worker for the
        // duration of the call.
        if (! processing.load (std::memory_order_acquire))
        {
            bool startOk = true;
            if (plugin->start_processing != nullptr)
            {
                instanceDispatcher.run ([&]
                {
                    ScopedClapDispatchRole role (ClapDispatchRole::Audio);
                    startOk = plugin->start_processing (plugin);
                });
            }

            if (! startOk)
            {
                buffer.clear();
                return;
            }
            processing.store (true, std::memory_order_release);
        }

        const auto frames = (uint32_t) buffer.getNumSamples();
        const auto availChans = (uint32_t) buffer.getNumChannels();

        // Wire JUCE channel pointers into our pre-allocated clap_audio_buffer_t
        // arrays.  inputChannelPtrs / outputChannelPtrs are sized to the
        // plugin's declared port channel counts at activate time.
        const auto inChans  = jmin (mainInputChannels,  availChans);
        const auto outChans = jmin (mainOutputChannels, availChans);

        for (uint32_t c = 0; c < inChans; ++c)
            inputChannelPtrs[c] = const_cast<float*> (buffer.getReadPointer ((int) c));
        for (uint32_t c = inChans; c < mainInputChannels; ++c)
            inputChannelPtrs[c] = silentChannel.getData();

        for (uint32_t c = 0; c < outChans; ++c)
            outputChannelPtrs[c] = buffer.getWritePointer ((int) c);
        for (uint32_t c = outChans; c < mainOutputChannels; ++c)
            outputChannelPtrs[c] = scratchOutChannel.getData();

        inAudioBuffer.data32        = inputChannelPtrs.getData();
        inAudioBuffer.data64        = nullptr;
        inAudioBuffer.channel_count = mainInputChannels;
        inAudioBuffer.latency       = 0;
        inAudioBuffer.constant_mask = 0;

        outAudioBuffer.data32        = outputChannelPtrs.getData();
        outAudioBuffer.data64        = nullptr;
        outAudioBuffer.channel_count = mainOutputChannels;
        outAudioBuffer.latency       = 0;
        outAudioBuffer.constant_mask = 0;

        // Convert this block's JUCE MIDI → CLAP events.  RT-safe:
        // ClapEventBridge storage is pre-allocated in activate().
        inputEventBridge.clear();
        for (const auto& meta : midiMessages)
            inputEventBridge.append (meta.getMessage(), meta.samplePosition);

        clap_input_events_t  inEvents  { &inputEventBridge,  &inputEventsSize,  &inputEventsGet };
        clap_output_events_t outEvents { nullptr,            &outputEventsTryPush };

        clap_process_t proc {};
        proc.steady_time         = steadyTime;
        proc.frames_count        = frames;
        proc.transport           = nullptr;
        proc.audio_inputs        = (mainInputChannels  > 0) ? &inAudioBuffer  : nullptr;
        proc.audio_outputs       = (mainOutputChannels > 0) ? &outAudioBuffer : nullptr;
        proc.audio_inputs_count  = (mainInputChannels  > 0) ? 1u : 0u;
        proc.audio_outputs_count = (mainOutputChannels > 0) ? 1u : 0u;
        proc.in_events           = &inEvents;
        proc.out_events          = &outEvents;

        // Clear JUCE's MIDI buffer — Phase D doesn't route plugin output
        // MIDI back yet, but we shouldn't leave incoming events in the
        // buffer for downstream nodes either.
        midiMessages.clear();

        clap_process_status status = CLAP_PROCESS_ERROR;
        instanceDispatcher.run ([&]
        {
            ScopedClapDispatchRole role (ClapDispatchRole::Audio);
            status = plugin->process (plugin, &proc);
        });

        if (status == CLAP_PROCESS_ERROR)
            buffer.clear();

        // For any host-requested channels beyond plugin's main output,
        // zero them — the plugin only wrote into mainOutputChannels.
        for (int c = (int) mainOutputChannels; c < buffer.getNumChannels(); ++c)
            buffer.clear (c, 0, (int) frames);

        steadyTime += (int64_t) frames;
    }

    bool hasEditor()             const override { return false; }   // Phase H
    AudioProcessorEditor* createEditor()         override { return nullptr; }

    int  getNumPrograms()                    override { return 1; }
    int  getCurrentProgram()                 override { return 0; }
    void setCurrentProgram (int)             override {}
    const String getProgramName (int)        override { return "Default"; }
    void changeProgramName (int, const String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {} // Phase G
    void setStateInformation (const void*, int)   override {} // Phase G

    bool   acceptsMidi()         const override { return true; }
    bool   producesMidi()        const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    void refreshParameterList() override {} // Phase E

protected:
    // Ctor + createAndInitialise are protected (not private) so a
    // non-headless GUI subclass (juce_CLAPPluginFormat.cpp) can share
    // this setup chain via the templated create() factory above.
    CLAPPluginInstance (CLAPModule::Ptr m,
                        const PluginDescription& desc,
                        const String& pluginId)
        : module (m),
          cachedDescription (desc),
          clapId (pluginId)
    {
        hostVtable.clap_version    = CLAP_VERSION;
        hostVtable.host_data       = this;
        hostVtable.name            = "JUCE-NSPA Winelib";
        hostVtable.vendor          = "";
        hostVtable.url             = "";
        hostVtable.version         = "1.0";
        hostVtable.get_extension   = &host_get_extension;
        hostVtable.request_restart = &host_request_restart;
        hostVtable.request_process = &host_request_process;
        hostVtable.request_callback = &host_request_callback;
    }

    bool createAndInitialise()
    {
        const auto* factory = module->getFactory();
        if (factory == nullptr || factory->create_plugin == nullptr)
            return false;

        const auto idUtf8 = clapId.toRawUTF8();

        winelib_log ("[CLAPInstance] create_plugin('%s') start", idUtf8);

        // create_plugin is [thread-safe] per spec but we run it on the
        // per-instance dispatcher so the plugin's first contact with us
        // is on its Win32 thread — same as VST3 phase 4's per-instance
        // dispatcher requirement.
        instanceDispatcher.run ([&]
        {
            ScopedClapDispatchRole role (ClapDispatchRole::Main);
            plugin = factory->create_plugin (factory, &hostVtable, idUtf8);
        });

        winelib_log ("[CLAPInstance] create_plugin -> %p", (const void*) plugin);

        if (plugin == nullptr)
        {
            winelib_log ("[CLAP] factory->create_plugin('%s') returned null", idUtf8);
            return false;
        }

        winelib_log ("[CLAPInstance] plugin->init() start");
        bool initOk = false;
        instanceDispatcher.run ([&]
        {
            ScopedClapDispatchRole role (ClapDispatchRole::Main);
            initOk = plugin->init (plugin);
        });
        winelib_log ("[CLAPInstance] plugin->init() -> %d", (int) initOk);

        if (! initOk)
        {
            winelib_log ("[CLAP] plugin->init() failed for '%s'", idUtf8);
            instanceDispatcher.run ([&]
            {
                ScopedClapDispatchRole role (ClapDispatchRole::Main);
                plugin->destroy (plugin);
            });
            plugin = nullptr;
            return false;
        }

        // Query audio-ports just enough to size processBlock buffers.
        // Full bus exposure to JUCE lands in Phase F; for Phase D we only
        // care about the main port channel count so we don't violate the
        // process() contract.
        queryAudioPortShape();
        winelib_log ("[CLAPInstance] createAndInitialise done");

        return true;
    }

    void teardown()
    {
        if (plugin == nullptr)
            return;

        if (processing.load (std::memory_order_acquire))
        {
            instanceDispatcher.run ([&]
            {
                ScopedClapDispatchRole role (ClapDispatchRole::Audio);
                if (plugin->stop_processing != nullptr)
                    plugin->stop_processing (plugin);
            });
            processing.store (false, std::memory_order_release);
        }

        if (activated.load (std::memory_order_acquire))
        {
            instanceDispatcher.run ([&]
            {
                ScopedClapDispatchRole role (ClapDispatchRole::Main);
                plugin->deactivate (plugin);
            });
            activated.store (false, std::memory_order_release);
        }

        instanceDispatcher.run ([&]
        {
            ScopedClapDispatchRole role (ClapDispatchRole::Main);
            plugin->destroy (plugin);
        });

        plugin = nullptr;
    }

    void queryAudioPortShape()
    {
        const clap_plugin_audio_ports_t* ports = nullptr;

        instanceDispatcher.run ([&]
        {
            ScopedClapDispatchRole role (ClapDispatchRole::Main);
            if (plugin->get_extension != nullptr)
                ports = static_cast<const clap_plugin_audio_ports_t*> (
                            plugin->get_extension (plugin, CLAP_EXT_AUDIO_PORTS));
        });

        if (ports == nullptr)
        {
            // Plugin doesn't expose audio-ports.  Default for synths is
            // 0-in/2-out, for effects 2-in/2-out.  Same fallback shape
            // makeDescriptionFromClapDescriptor uses.
            mainInputChannels  = cachedDescription.isInstrument ? 0 : 2;
            mainOutputChannels = 2;
            allocateChannelPointerArrays();
            return;
        }

        uint32_t inputCount  = 0;
        uint32_t outputCount = 0;
        instanceDispatcher.run ([&]
        {
            ScopedClapDispatchRole role (ClapDispatchRole::Main);
            inputCount  = ports->count (plugin, true);
            outputCount = ports->count (plugin, false);
        });

        clap_audio_port_info_t mainIn  {};
        clap_audio_port_info_t mainOut {};
        bool gotIn = false, gotOut = false;

        if (inputCount > 0)
            instanceDispatcher.run ([&]
            {
                ScopedClapDispatchRole role (ClapDispatchRole::Main);
                gotIn = ports->get (plugin, 0, true, &mainIn);
            });

        if (outputCount > 0)
            instanceDispatcher.run ([&]
            {
                ScopedClapDispatchRole role (ClapDispatchRole::Main);
                gotOut = ports->get (plugin, 0, false, &mainOut);
            });

        mainInputChannels  = gotIn  ? mainIn.channel_count  : 0;
        mainOutputChannels = gotOut ? mainOut.channel_count : 2;

        winelib_log ("[CLAP] '%s' main ports: %u in / %u out",
                     cachedDescription.name.toRawUTF8(),
                     mainInputChannels, mainOutputChannels);

        allocateChannelPointerArrays();
    }

    void allocateChannelPointerArrays()
    {
        if (mainInputChannels > 0)
            inputChannelPtrs.calloc (mainInputChannels);
        if (mainOutputChannels > 0)
            outputChannelPtrs.calloc (mainOutputChannels);

        // Scratch silent input + dummy output channel for filling
        // plugin-required channels that JUCE didn't provide.  Sized to
        // a generous block-cap; reallocate inside prepareToPlay if a
        // larger block size is set.
        const auto blockCap = jmax (4096, currentBlockSize);
        silentChannel.calloc ((size_t) blockCap);
        scratchOutChannel.calloc ((size_t) blockCap);

        // Pre-allocate the MIDI event bridge storage so processBlock
        // never allocates on the audio thread.
        inputEventBridge.ensureCapacity();
    }

    //==============================================================================
    // Host vtable C callbacks — invoked by the plugin.  Spec: all three
    // request_* are [thread-safe], so they may fire from process() on the
    // audio thread.  These must be lock-free + allocation-free.

    static const void* CLAP_ABI host_get_extension (const clap_host_t* host, const char* id)
    {
        if (host == nullptr || id == nullptr)
            return nullptr;

        winelib_log ("[CLAPHost] get_extension('%s')", id);

        const String idStr (id);

        // Logging — plugin writes diagnostics via host log.  Some plugins
        // query this and silently degrade if absent; route their messages
        // through our trace so we see what they're doing.
        if (idStr == CLAP_EXT_LOG)
            return &hostLogExt;

        // Thread check — plugin asks "is this the main thread / audio thread?".
        // Without this, plugins that assert thread identity may bail out
        // of their setup logic.
        if (idStr == CLAP_EXT_THREAD_CHECK)
            return &hostThreadCheckExt;

        // GUI — plugin uses these to request the host resize the editor
        // window, signal closure, etc.  Required for editor instantiation
        // via createEditor() (see juce_CLAPPluginFormat.cpp).
        if (idStr == CLAP_EXT_GUI)
            return &hostGuiExt;

        // Phase E/F/G expose: params, audio-ports, note-ports, state,
        // timer-support, posix-fd-support.
        return nullptr;
    }

    static void CLAP_ABI host_request_restart (const clap_host_t* host)
    {
        if (auto* self = static_cast<CLAPPluginInstance*> (host->host_data))
            self->restartRequested.store (true, std::memory_order_release);
    }

    static void CLAP_ABI host_request_process (const clap_host_t* host)
    {
        if (auto* self = static_cast<CLAPPluginInstance*> (host->host_data))
            self->processRequested.store (true, std::memory_order_release);
    }

    static void CLAP_ABI host_request_callback (const clap_host_t* host)
    {
        if (auto* self = static_cast<CLAPPluginInstance*> (host->host_data))
            self->mainThreadCallbackRequested.store (true, std::memory_order_release);
    }

    //==============================================================================
    // Host log extension — plugin uses this for diagnostic messages.
    // Routed through winelib_log so plugin messages show up alongside
    // the host's own trace points.

    static void CLAP_ABI host_log (const clap_host_t* /*host*/,
                                   clap_log_severity severity, const char* msg)
    {
        const char* sev = "?";
        switch (severity)
        {
            case CLAP_LOG_DEBUG:               sev = "DEBUG"; break;
            case CLAP_LOG_INFO:                sev = "INFO";  break;
            case CLAP_LOG_WARNING:             sev = "WARN";  break;
            case CLAP_LOG_ERROR:               sev = "ERROR"; break;
            case CLAP_LOG_FATAL:               sev = "FATAL"; break;
            case CLAP_LOG_HOST_MISBEHAVING:    sev = "HOST_MISBEHAVING";   break;
            case CLAP_LOG_PLUGIN_MISBEHAVING:  sev = "PLUGIN_MISBEHAVING"; break;
            default: break;
        }
        winelib_log ("[plugin-log %s] %s", sev, msg != nullptr ? msg : "(null)");
    }

    //==============================================================================
    // Host thread-check extension — plugin queries which thread it's on.
    // We track the main thread id at instance construction; the audio
    // thread is whichever thread calls processBlock.

    static bool CLAP_ABI host_is_main_thread (const clap_host_t* host)
    {
        // True iff this thread is currently dispatching a main-thread
        // call on the plugin's behalf (clap_current_role==Main) OR is
        // the actual JUCE message thread (host_data captures it).  The
        // former covers our Win32-dispatcher-worker marshaling case;
        // the latter handles direct main-thread invocation (eg. if a
        // host extension callback fires from the message thread).
        if (clap_current_role == ClapDispatchRole::Main)
            return true;
        if (auto* self = static_cast<CLAPPluginInstance*> (host->host_data))
            return std::this_thread::get_id() == self->mainThreadId;
        return false;
    }

    static bool CLAP_ABI host_is_audio_thread (const clap_host_t* host)
    {
        if (clap_current_role == ClapDispatchRole::Audio)
            return true;
        if (auto* self = static_cast<CLAPPluginInstance*> (host->host_data))
            return std::this_thread::get_id() == self->audioThreadId.load (std::memory_order_acquire);
        return false;
    }

    //==============================================================================
    // Host GUI extension — plugin uses these to ask the host to resize
    // our editor component, request show/hide, or notify of external
    // closure.  Plugin's own GUI handshake (is_api_supported/create/
    // set_parent/show) is driven by the CLAPPluginEditor on the
    // juce_audio_processors non-headless side.

    static void CLAP_ABI host_resize_hints_changed (const clap_host_t*)
    {
        // No-op for now — the editor queries get_resize_hints on demand
        // when JUCE invokes resized().
    }

    static bool CLAP_ABI host_request_resize (const clap_host_t* host,
                                              uint32_t width, uint32_t height)
    {
        if (auto* self = static_cast<CLAPPluginInstance*> (host->host_data))
        {
            self->pendingEditorResizeW.store (width,  std::memory_order_release);
            self->pendingEditorResizeH.store (height, std::memory_order_release);
            self->pendingEditorResize.store (true,    std::memory_order_release);
            return true; // acknowledged; main-thread tick will apply
        }
        return false;
    }

    static bool CLAP_ABI host_request_show (const clap_host_t*) { return true; }
    static bool CLAP_ABI host_request_hide (const clap_host_t*) { return true; }

    static void CLAP_ABI host_closed (const clap_host_t* host, bool /*was_destroyed*/)
    {
        if (auto* self = static_cast<CLAPPluginInstance*> (host->host_data))
            self->editorClosedByPlugin.store (true, std::memory_order_release);
    }

    //==============================================================================
    CLAPModule::Ptr module;
    PluginDescription cachedDescription;
    String clapId;

    const clap_plugin_t* plugin = nullptr;
    clap_host_t          hostVtable {};

    // Host extension vtables — handed to the plugin via host_get_extension.
    static constexpr clap_host_log_t hostLogExt {
        &CLAPPluginInstance::host_log
    };
    static constexpr clap_host_thread_check_t hostThreadCheckExt {
        &CLAPPluginInstance::host_is_main_thread,
        &CLAPPluginInstance::host_is_audio_thread
    };
    static constexpr clap_host_gui_t hostGuiExt {
        &CLAPPluginInstance::host_resize_hints_changed,
        &CLAPPluginInstance::host_request_resize,
        &CLAPPluginInstance::host_request_show,
        &CLAPPluginInstance::host_request_hide,
        &CLAPPluginInstance::host_closed
    };

    WineWin32Dispatcher instanceDispatcher;

    // Thread identity tracking for clap_host_thread_check.  Main thread
    // captured at construction (likely JUCE message thread).  Audio thread
    // captured on first processBlock entry — typically the host's RT
    // audio pthread.
    std::thread::id              mainThreadId  = std::this_thread::get_id();
    std::atomic<std::thread::id> audioThreadId { std::thread::id() };

    std::atomic<bool> activated  { false };
    std::atomic<bool> processing { false };
    std::atomic<bool> restartRequested            { false };
    std::atomic<bool> processRequested            { false };
    std::atomic<bool> mainThreadCallbackRequested { false };

    double currentSampleRate = 0.0;
    int    currentBlockSize  = 0;
    int64_t steadyTime       = 0;

    uint32_t mainInputChannels  = 0;
    uint32_t mainOutputChannels = 0;

    HeapBlock<float*> inputChannelPtrs;
    HeapBlock<float*> outputChannelPtrs;
    HeapBlock<float>  silentChannel;
    HeapBlock<float>  scratchOutChannel;

    clap_audio_buffer_t inAudioBuffer  {};
    clap_audio_buffer_t outAudioBuffer {};

    ClapEventBridge inputEventBridge;

    //==============================================================================
    // Plugin-requested editor resize / external close — drained on the
    // main thread by CLAPPluginEditor.  All writes are from host extension
    // callbacks (potentially audio-thread per spec [thread-safe]); reads
    // happen on the editor's main-thread tick.  Atomics give safe handoff.
public:
    std::atomic<bool>     pendingEditorResize       { false };
    std::atomic<uint32_t> pendingEditorResizeW      { 0 };
    std::atomic<uint32_t> pendingEditorResizeH      { 0 };
    std::atomic<bool>     editorClosedByPlugin      { false };

    // The CLAPPluginEditor (non-headless module) drives plugin->gui->*
    // calls — it needs the per-instance dispatcher + plugin pointer.
    // Exposed here (public) for that cross-module access.
    const clap_plugin_t* getPlugin() const noexcept       { return plugin; }
    WineWin32Dispatcher& getInstanceDispatcher() noexcept { return instanceDispatcher; }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CLAPPluginInstance)
};
