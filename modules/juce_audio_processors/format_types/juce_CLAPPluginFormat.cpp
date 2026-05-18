/* SPDX-License-Identifier: ISC
 *
 * juce_CLAPPluginFormat.cpp — CLAP plugin format GUI variant + editor.
 *
 * Adds a CLAPPluginInstanceWithEditor subclass of the headless
 * CLAPPluginInstance that overrides hasEditor()/createEditor() to drive
 * the plugin's clap.gui extension, plus a CLAPPluginEditor that owns a
 * juce::WineHWNDEmbedComponent — same in-process Wine HWND → JUCE X11
 * peer reparenting that the VST3 host uses (juce_VST3PluginFormat.cpp's
 * VST3PluginWindow).
 *
 * GUI handshake (per clap/ext/gui.h spec):
 *    1. gui->is_api_supported(WIN32, false)         — winelib only supports WIN32
 *    2. gui->create(WIN32, false)                   — embedded, not floating
 *    3. gui->get_size(&w, &h)                       — initial size
 *    4. gui->set_parent(&clap_window_t{WIN32, hwnd})  — attach into our HWND
 *    5. gui->show()                                 — render
 *    6. gui->hide(); gui->destroy()                 — teardown
 *
 * All plugin GUI calls are dispatched through the instance's
 * WineWin32Dispatcher with ClapDispatchRole::Main so plugin sees its
 * "main thread" identity the way it expects.
 */

#if JUCE_INTERNAL_HAS_CLAP

// CLAP headers via the headless module's vendored copy.  juce_clap_headers
// INTERFACE target only adds the include dir for juce_audio_processors_headless;
// reference the file by its full module-relative path here.
#include <juce_audio_processors_headless/format_types/clap/juce_clap_winelib_abi.h>

namespace juce
{

#include <juce_audio_processors_headless/format_types/juce_CLAPPluginFormatImpl.h>

namespace
{

//==============================================================================
class CLAPPluginInstanceWithEditor;

class CLAPPluginEditor final : public AudioProcessorEditor,
                               private Timer
{
public:
    CLAPPluginEditor (CLAPPluginInstanceWithEditor& inst, const clap_plugin_gui_t* g);
    ~CLAPPluginEditor() override;

    void resized() override;

private:
    void timerCallback() override;

    CLAPPluginInstanceWithEditor& instance;
    const clap_plugin_gui_t* const gui;
    WineHWNDEmbedComponent embedded { 128, 128 };
    bool guiCreated = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CLAPPluginEditor)
};


//==============================================================================
class CLAPPluginInstanceWithEditor final : public CLAPPluginInstance
{
public:
    using CLAPPluginInstance::CLAPPluginInstance;

    bool hasEditor() const override
    {
        return resolveGui() != nullptr;
    }

    AudioProcessorEditor* createEditor() override
    {
        if (const auto* g = resolveGui())
            return new CLAPPluginEditor (*this, g);
        return nullptr;
    }

private:
    // clap.gui is fetched on the plugin's main thread.  CLAP spec:
    // plugin->get_extension is [thread-safe], so direct call from the
    // JUCE message thread (which holds the "main thread" identity)
    // is correct — no dispatcher hop needed.  Same reasoning as the
    // CLAPPluginEditor ctor's direct calls.
    const clap_plugin_gui_t* resolveGui() const
    {
        if (cachedGui != nullptr || guiQueried)
            return cachedGui;

        auto* plug = getPlugin();
        if (plug == nullptr || plug->get_extension == nullptr)
            return nullptr;

        cachedGui = static_cast<const clap_plugin_gui_t*> (
                        plug->get_extension (plug, CLAP_EXT_GUI));
        guiQueried = true;
        return cachedGui;
    }

    mutable const clap_plugin_gui_t* cachedGui = nullptr;
    mutable bool guiQueried = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CLAPPluginInstanceWithEditor)
};


//==============================================================================
CLAPPluginEditor::CLAPPluginEditor (CLAPPluginInstanceWithEditor& inst,
                                    const clap_plugin_gui_t* g)
    : AudioProcessorEditor (inst), instance (inst), gui (g)
{
    setOpaque (true);

    // GUI calls are ALL invoked directly on the JUCE message thread —
    // NOT routed through instanceDispatcher.run.  Rationale: WineHWNDEmbedComponent's
    // 60Hz Win32 message pump (its JUCE Timer) also runs on the JUCE
    // message thread.  If we blocked the message thread inside
    // dispatcher.run waiting for the worker, the plugin's gui->set_parent /
    // gui->show would deadlock waiting on a Win32 SendMessage ack that
    // would only be processed when the pump fires — which can't happen
    // until set_parent returns.  Mirrors the VST3 path's direct
    // view->attached() on the message thread (juce_VST3PluginFormat.cpp's
    // VST3PluginWindow::attachPluginWindow — no dispatcher wrap there).
    //
    // Thread-check identity is asserted via a ScopedClapDispatchRole::Main
    // marker covering the handshake.  The thread_local role flag is what
    // host_is_main_thread answers on; no dispatcher hop, so the deadlock
    // rationale above still holds.  This makes the answer robust even if
    // the message thread differs from whichever thread happened to
    // construct CLAPPluginInstance (eg. async plugin load on a worker).
    ScopedClapDispatchRole role (ClapDispatchRole::Main);

    auto* plug = instance.getPlugin();

    // 1. Check API support — winelib only supports CLAP_WINDOW_API_WIN32.
    if (! gui->is_api_supported (plug, CLAP_WINDOW_API_WIN32, false))
    {
        // No Win32 GUI support — leave editor empty; JUCE will still show
        // an empty window but at least we don't try to create a
        // mismatched embed.
        return;
    }

    // 2. Allocate plugin-side GUI resources (embedded mode).
    if (! gui->create (plug, CLAP_WINDOW_API_WIN32, false))
        return;

    guiCreated = true;

    // 3. Initial size from plugin.
    uint32_t w = 0, h = 0;
    if (gui->get_size != nullptr)
        gui->get_size (plug, &w, &h);

    if (w == 0 || h == 0) { w = 800; h = 600; }
    setSize ((int) w, (int) h);

    embedded.setBounds (getLocalBounds());
    addAndMakeVisible (embedded);

    // 4. Embed our HWND into the plugin's GUI tree.  set_parent is the
    //    moment Wine's wine_x11_window for our HWND becomes the parent
    //    of the plugin's child HWNDs / X11 children.
    clap_window_t window {};
    window.api    = CLAP_WINDOW_API_WIN32;
    window.win32  = embedded.getHWND();

    if (! gui->set_parent (plug, &window))
        return;

    // 5. Show.
    gui->show (plug);

    // Drive plugin-requested resizes / closures on a main-thread tick.
    startTimerHz (30);
}

CLAPPluginEditor::~CLAPPluginEditor()
{
    stopTimer();

    if (! guiCreated || gui == nullptr)
        return;

    auto* plug = instance.getPlugin();
    if (plug == nullptr) return;

    // Direct on JUCE message thread — same rationale as the ctor.  Role
    // marker mirrors the ctor's so host_is_main_thread answers true via
    // the thread_local fast path.  WineHWNDEmbedComponent's destructor
    // handles the deferred X11 unmap + reparent-to-root + delayed
    // DestroyWindow that the VST3 path uses to keep wine_x11_window-
    // targeted X11 events from racing the teardown.
    ScopedClapDispatchRole role (ClapDispatchRole::Main);

    gui->hide (plug);
    gui->destroy (plug);
}

void CLAPPluginEditor::resized()
{
    embedded.setBounds (getLocalBounds());

    if (! guiCreated || gui == nullptr || gui->set_size == nullptr)
        return;

    auto* plug = instance.getPlugin();
    if (plug == nullptr) return;

    const uint32_t w = (uint32_t) jmax (1, getWidth());
    const uint32_t h = (uint32_t) jmax (1, getHeight());

    ScopedClapDispatchRole role (ClapDispatchRole::Main);
    gui->set_size (plug, w, h);
}

void CLAPPluginEditor::timerCallback()
{
    // Plugin-requested resize?  host_request_resize stashed dimensions
    // in atomic fields on the instance.  setSize re-enters
    // CLAPPluginEditor::resized which carries its own role marker.
    ScopedClapDispatchRole role (ClapDispatchRole::Main);

    if (instance.pendingEditorResize.exchange (false, std::memory_order_acq_rel))
    {
        const auto w = (int) instance.pendingEditorResizeW.load (std::memory_order_acquire);
        const auto h = (int) instance.pendingEditorResizeH.load (std::memory_order_acquire);
        if (w > 0 && h > 0)
            setSize (w, h);
    }

    // Plugin signaled external close?
    if (instance.editorClosedByPlugin.exchange (false, std::memory_order_acq_rel))
    {
        if (auto* dw = findParentComponentOfClass<DocumentWindow>())
            dw->closeButtonPressed();
    }
}

} // anonymous namespace


//==============================================================================
void CLAPPluginFormat::createPluginInstance (const PluginDescription& description,
                                             double initialSampleRate,
                                             int initialBufferSize,
                                             PluginCreationCallback callback)
{
    if (callback == nullptr)
        return;

    const File file (description.fileOrIdentifier);
    auto module = CLAPModule::getOrCreate (file);

    if (module == nullptr)
    {
        callback (nullptr, "CLAP module load failed: " + file.getFullPathName());
        return;
    }

    const auto* factory = module->getFactory();
    if (factory == nullptr)
    {
        callback (nullptr, "CLAP factory unavailable");
        return;
    }

    // Match by hash of clap id (same convention as the headless format).
    uint32_t count = 0;
    globalWineDispatcher().run ([&] { count = factory->get_plugin_count (factory); });

    String matchedClapId;
    for (uint32_t i = 0; i < count; ++i)
    {
        const clap_plugin_descriptor_t* d = nullptr;
        globalWineDispatcher().run ([&] { d = factory->get_plugin_descriptor (factory, i); });
        if (d == nullptr || d->id == nullptr)
            continue;
        const String idStr (d->id);
        if ((int) idStr.hashCode() == description.uniqueId)
        {
            matchedClapId = idStr;
            break;
        }
    }

    if (matchedClapId.isEmpty())
    {
        callback (nullptr, "CLAP plugin id not found in factory: " + description.name);
        return;
    }

    auto instance = CLAPPluginInstance::create<CLAPPluginInstanceWithEditor>
                        (module, description, matchedClapId);

    if (instance == nullptr)
    {
        callback (nullptr, "CLAP instance creation failed: " + description.name);
        return;
    }

    instance->setRateAndBufferSizeDetails (initialSampleRate, initialBufferSize);
    callback (std::move (instance), {});
}

} // namespace juce

#endif
