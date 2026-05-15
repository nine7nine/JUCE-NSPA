/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

#pragma once

#if JUCE_INTERNAL_HAS_VST

//==============================================================================
#undef PRAGMA_ALIGN_SUPPORTED


#if ! JUCE_MSVC
 #define __cdecl
#endif

JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wzero-as-null-pointer-constant")
JUCE_BEGIN_IGNORE_WARNINGS_MSVC (4996)

#define VST_FORCE_DEPRECATED 0

namespace Vst2
{
struct AEffect;

// If the following files cannot be found then you are probably trying to host
// VST2 plug-ins. To do this you must have a VST2 SDK in your header search
// paths or use the "VST (Legacy) SDK Folder" field in the Projucer. The VST2
// SDK can be obtained from the vstsdk3610_11_06_2018_build_37 (or older) VST3
// SDK or JUCE version 5.3.2.
#include <pluginterfaces/vst2.x/aeffect.h>
#include <pluginterfaces/vst2.x/aeffectx.h>
}

#include <juce_audio_processors_headless/format_types/juce_VSTCommon.h>

JUCE_END_IGNORE_WARNINGS_MSVC
JUCE_END_IGNORE_WARNINGS_GCC_LIKE

JUCE_BEGIN_IGNORE_WARNINGS_MSVC (4355)
JUCE_BEGIN_IGNORE_DEPRECATION_WARNINGS

#include <juce_audio_processors_headless/format_types/juce_VSTMidiEventList.h>

#if ! JUCE_WINDOWS
 static void _fpreset() {}
 static void _clearfp() {}
#endif

#ifndef JUCE_VST_WRAPPER_LOAD_CUSTOM_MAIN
 #define JUCE_VST_WRAPPER_LOAD_CUSTOM_MAIN
#endif

#ifndef JUCE_VST_WRAPPER_INVOKE_MAIN
#define JUCE_VST_WRAPPER_INVOKE_MAIN  effect = module->moduleMain (audioMaster);
#endif

#ifndef JUCE_VST_FALLBACK_HOST_NAME
 #define JUCE_VST_FALLBACK_HOST_NAME "Juce VST Host"
#endif

//==============================================================================
namespace juce
{

/* winelib patch: when building under winegcc (`__WINE__` defined) on
 * Linux, detect Windows PE VST2 plugins and load them via Wine's
 * `LoadLibraryW` instead of dlopen.  All per-instance plugin entry
 * points (factory `moduleMain`, then every `dispatcher(opcode, ...)`
 * call) marshal onto a per-instance Win32 worker so the plugin sees
 * one stable Win32 thread identity for its entire lifetime — same
 * pattern Phase 4 introduced for VST3 hosting (see commit fc363bb +
 * juce_VST3PluginFormatImpl.h).  Vst2::audioMasterCallback runs on
 * whatever thread the plugin chooses (already inside the worker), so
 * we do NOT route the host-callback path. */
#if JUCE_LINUX && defined (__WINE__)
 #define JUCE_VST2_WINELIB 1
#endif

#if JUCE_VST2_WINELIB
#include "juce_winelib_path.h"
#include "juce_winelib_dispatch.h"

/* ms_abi call-boundary helpers for VST2 winelib hosting.
 *
 * The Steinberg VST2 SDK defines `VSTCALLBACK` as empty under
 * `__GNUC__` (commented-out __cdecl), so JUCE's typedefs for
 * `audioMasterCallback`, `AEffectDispatcherProc`,
 * `AEffectProcessReplacingProc`, etc. all carry the default System V
 * calling convention on x86_64.  Windows PE VST2 DLLs were compiled
 * with `VSTCALLBACK = __cdecl`, which on x86_64 is the Microsoft x64
 * ABI.  Direct function-pointer calls between System V and MS x64
 * shuffle args through different registers (rdi/rsi/rdx/rcx vs
 * rcx/rdx/r8/r9) — plugin sees garbage and either returns nullptr
 * from moduleMain or fails internal sanity checks during effOpen.
 *
 * We can't extend Steinberg's SDK headers (license + system-package
 * file) and we can't redefine VSTCALLBACK before include (the SDK
 * sets it unconditionally under __GNUC__).  So instead we reinterpret
 * the PE-origin function pointers as ms_abi-typed pointers at each
 * call site.  The cast is safe because the underlying machine code
 * really is ms_abi — we're only correcting the type C++ sees.
 *
 * Yabridge solves the same problem via vestige's VST_CALL_CONV macro
 * which under __WINE__ resolves to winegcc's __cdecl (= ms_abi); we
 * can't take that path because the JUCE-as-host build needs the full
 * Steinberg surface (ERect, effVendorSpecific, kVstTransportRecording,
 * etc.) that vestige lacks.  See feedback_vestige_cleanroom_no_extend.
 */
/* Already inside `namespace juce` (impl.h opens it at line ~93 below
 * us in this file) when this header included from
 * juce_VSTPluginFormatHeadless.cpp / juce_VSTPluginFormat.cpp.
 * Don't re-wrap or we get juce::juce::winelib_vst2_abi. */
namespace winelib_vst2_abi
{
    using DispatcherMS         = pointer_sized_int __attribute__((__ms_abi__)) (*) (Vst2::AEffect*, Vst2::VstInt32, Vst2::VstInt32, Vst2::VstIntPtr, void*, float);
    using ProcessReplacingMS   = void              __attribute__((__ms_abi__)) (*) (Vst2::AEffect*, float**, float**, Vst2::VstInt32);
    using ProcessDoubleMS      = void              __attribute__((__ms_abi__)) (*) (Vst2::AEffect*, double**, double**, Vst2::VstInt32);
    using ProcessLegacyMS      = void              __attribute__((__ms_abi__)) (*) (Vst2::AEffect*, float**, float**, Vst2::VstInt32);
    using AudioMasterMS        = Vst2::VstIntPtr   __attribute__((__ms_abi__)) (*) (Vst2::AEffect*, Vst2::VstInt32, Vst2::VstInt32, Vst2::VstIntPtr, void*, float);
    using VstMainMS            = Vst2::AEffect*    __attribute__((__ms_abi__)) (*) (AudioMasterMS);

    // Inline call helpers — cast the System-V-typed function pointer
    // we have to the matching ms_abi-typed pointer, then invoke.  The
    // cast is purely a type fix; the underlying machine code is
    // already ms_abi (it came from a Windows PE DLL).
    inline pointer_sized_int callDispatcher (Vst2::AEffect* e, Vst2::VstInt32 op, Vst2::VstInt32 idx, Vst2::VstIntPtr val, void* ptr, float opt)
    {
        return reinterpret_cast<DispatcherMS> (e->dispatcher) (e, op, idx, val, ptr, opt);
    }

    inline void callProcessReplacing (Vst2::AEffect* e, float** in, float** out, Vst2::VstInt32 numFrames)
    {
        reinterpret_cast<ProcessReplacingMS> (e->processReplacing) (e, in, out, numFrames);
    }

    inline void callProcessDoubleReplacing (Vst2::AEffect* e, double** in, double** out, Vst2::VstInt32 numFrames)
    {
        reinterpret_cast<ProcessDoubleMS> (e->processDoubleReplacing) (e, in, out, numFrames);
    }

    inline void callProcessLegacy (Vst2::AEffect* e, float** in, float** out, Vst2::VstInt32 numFrames)
    {
        reinterpret_cast<ProcessLegacyMS> (e->process) (e, in, out, numFrames);
    }

    inline Vst2::AEffect* callMain (Vst2::AEffect* (*mainFn) (Vst2::audioMasterCallback), AudioMasterMS audioMaster)
    {
        // mainFn was obtained via GetProcAddress on a PE DLL — the
        // pointer's machine code is ms_abi.  audioMaster is our
        // ms_abi-attributed thunk — passing it through under the
        // AudioMasterMS typedef preserves the attribute.
        auto wineMain = reinterpret_cast<VstMainMS> (mainFn);
        return wineMain (audioMaster);
    }
}
#endif

//==============================================================================
namespace
{
    const int fxbVersionNum = 1;

    struct fxProgram
    {
        int32 chunkMagic;    // 'CcnK'
        int32 byteSize;      // of this chunk, excl. magic + byteSize
        int32 fxMagic;       // 'FxCk'
        int32 version;
        int32 fxID;          // fx unique id
        int32 fxVersion;
        int32 numParams;
        char prgName[28];
        float params[1];        // variable no. of parameters
    };

    struct fxSet
    {
        int32 chunkMagic;    // 'CcnK'
        int32 byteSize;      // of this chunk, excl. magic + byteSize
        int32 fxMagic;       // 'FxBk'
        int32 version;
        int32 fxID;          // fx unique id
        int32 fxVersion;
        int32 numPrograms;
        char future[128];
        fxProgram programs[1];  // variable no. of programs
    };

    struct fxChunkSet
    {
        int32 chunkMagic;    // 'CcnK'
        int32 byteSize;      // of this chunk, excl. magic + byteSize
        int32 fxMagic;       // 'FxCh', 'FPCh', or 'FBCh'
        int32 version;
        int32 fxID;          // fx unique id
        int32 fxVersion;
        int32 numPrograms;
        char future[128];
        int32 chunkSize;
        char chunk[8];          // variable
    };

    struct fxProgramSet
    {
        int32 chunkMagic;    // 'CcnK'
        int32 byteSize;      // of this chunk, excl. magic + byteSize
        int32 fxMagic;       // 'FxCh', 'FPCh', or 'FBCh'
        int32 version;
        int32 fxID;          // fx unique id
        int32 fxVersion;
        int32 numPrograms;
        char name[28];
        int32 chunkSize;
        char chunk[8];          // variable
    };

    // Compares a magic value in either endianness.
    static bool compareMagic (int32 magic, const char* name) noexcept
    {
        return magic == (int32) ByteOrder::littleEndianInt (name)
            || magic == (int32) ByteOrder::bigEndianInt (name);
    }

    static int32 fxbName (const char* name) noexcept    { return (int32) ByteOrder::littleEndianInt (name); }
    static int32 fxbSwap (int32 x) noexcept             { return (int32) ByteOrder::swapIfLittleEndian ((uint32) x); }

    static float fxbSwapFloat (const float x) noexcept
    {
       #ifdef JUCE_LITTLE_ENDIAN
        union { uint32 asInt; float asFloat; } n;
        n.asFloat = x;
        n.asInt = ByteOrder::swap (n.asInt);
        return n.asFloat;
       #else
        return x;
       #endif
    }
}

//==============================================================================
namespace
{
    static double getVSTHostTimeNanoseconds() noexcept
    {
       #if JUCE_WINDOWS
        return timeGetTime() * 1000000.0;
       #elif JUCE_LINUX || JUCE_BSD || JUCE_IOS || JUCE_ANDROID
        timeval micro;
        gettimeofday (&micro, nullptr);
        return (double) micro.tv_usec * 1000.0;
       #elif JUCE_MAC
        UnsignedWide micro;
        Microseconds (&micro);
        return micro.lo * 1000.0;
       #endif
    }

    static int shellUIDToCreate = 0;
    static int insideVSTCallback = 0;

    struct IdleCallRecursionPreventer
    {
        IdleCallRecursionPreventer()  : isMessageThread (MessageManager::getInstance()->isThisTheMessageThread())
        {
            if (isMessageThread)
                ++insideVSTCallback;
        }

        ~IdleCallRecursionPreventer()
        {
            if (isMessageThread)
                --insideVSTCallback;
        }

        const bool isMessageThread;
        JUCE_DECLARE_NON_COPYABLE (IdleCallRecursionPreventer)
    };

   #if JUCE_MAC
    static bool makeFSRefFromPath (FSRef* destFSRef, const String& path)
    {
        return FSPathMakeRef (reinterpret_cast<const UInt8*> (path.toRawUTF8()), destFSRef, nullptr) == noErr;
    }
   #endif
}

//==============================================================================
using MainCall = Vst2::AEffect* (VSTCALLBACK*) (Vst2::audioMasterCallback);

//==============================================================================
// Change this to disable logging of various VST activities
#ifndef VST_LOGGING
 #define VST_LOGGING 1
#endif

#if VST_LOGGING
 #define JUCE_VST_LOG(a) Logger::writeToLog(a);
#else
 #define JUCE_VST_LOG(a)
#endif

//==============================================================================
class VSTXMLInfo
{
public:
    static VSTXMLInfo* createFor (const juce::XmlElement& xml)
    {
        if (xml.hasTagName ("VSTParametersStructure"))
            return new VSTXMLInfo (xml);

        if (const auto* x = xml.getChildByName ("VSTParametersStructure"))
            return new VSTXMLInfo (*x);

        return nullptr;
    }

    struct Group;

    struct Base
    {
        Base() noexcept {}
        virtual ~Base() {}

        Group* parent = nullptr;
    };

    struct Param final : public Base
    {
        int paramID;
        juce::String expr, name, label;
        juce::StringArray shortNames;
        juce::String type;
        int numberOfStates;
        float defaultValue;
    };

    struct Group final : public Base
    {
        juce::String name;
        juce::OwnedArray<Base> paramTree;
    };

    struct Range
    {
        Range() noexcept {}
        Range (const juce::String& s)       { set (s); }

        void set (const juce::String& s)
        {
            inclusiveLow  = s.startsWithChar ('[');
            inclusiveHigh = s.endsWithChar   (']');

            auto str = s.removeCharacters ("[]");

            low  = str.upToFirstOccurrenceOf (",", false, false).getFloatValue();
            high = str.fromLastOccurrenceOf  (",", false, false).getFloatValue();
        }

        bool contains (float f) const noexcept
        {
            return (inclusiveLow  ? (f >= low)  : (f > low))
                && (inclusiveHigh ? (f <= high) : (f < high));
        }

        float low = 0;
        float high = 0;

        bool inclusiveLow = false;
        bool inclusiveHigh = false;
    };

    struct Entry
    {
        juce::String name;
        Range range;
    };

    struct ValueType
    {
        juce::String name, label;
        juce::OwnedArray<Entry> entries;
    };

    struct Template
    {
        juce::String name;
        juce::OwnedArray<Param> params;
    };

    const Param* getParamForID (const int paramID, const Group* const grp) const
    {
        for (auto item : (grp != nullptr ? grp->paramTree : paramTree))
        {
            if (auto param = dynamic_cast<const Param*> (item))
                if (param->paramID == paramID)
                    return param;

            if (auto group = dynamic_cast<const Group*> (item))
                if (auto res = getParamForID (paramID, group))
                    return res;
        }

        return nullptr;
    }

    const ValueType* getValueType (const juce::String& name) const
    {
        for (auto v : valueTypes)
            if (v->name == name)
                return v;

        return nullptr;
    }

    juce::OwnedArray<Base> paramTree;
    juce::OwnedArray<ValueType> valueTypes;
    juce::OwnedArray<Template> templates;

    ValueType switchValueType;

private:
    VSTXMLInfo (const juce::XmlElement& xml)
    {
        switchValueType.entries.add (new Entry ({ TRANS ("Off"), Range ("[0, 0.5[") }));
        switchValueType.entries.add (new Entry ({ TRANS ("On"),  Range ("[0.5, 1]") }));

        for (auto* item : xml.getChildIterator())
        {
            if (item->hasTagName ("Param"))           parseParam (*item, nullptr, nullptr);
            else if (item->hasTagName ("ValueType"))  parseValueType (*item);
            else if (item->hasTagName ("Template"))   parseTemplate (*item);
            else if (item->hasTagName ("Group"))      parseGroup (*item, nullptr);
        }
    }

    void parseParam (const juce::XmlElement& item, Group* group, Template* temp)
    {
        auto param = new Param();

        if (temp != nullptr)
            param->expr = item.getStringAttribute ("id");
        else
            param->paramID = item.getIntAttribute ("id");

        param->name           = item.getStringAttribute ("name");
        param->label          = item.getStringAttribute ("label");
        param->type           = item.getStringAttribute ("type");
        param->numberOfStates = item.getIntAttribute ("numberOfStates");
        param->defaultValue   = (float) item.getDoubleAttribute ("defaultValue");

        param->shortNames.addTokens (item.getStringAttribute ("shortName"), ",", juce::StringRef());
        param->shortNames.trim();
        param->shortNames.removeEmptyStrings();

        if (group != nullptr)
        {
            group->paramTree.add (param);
            param->parent = group;
        }
        else if (temp != nullptr)
        {
            temp->params.add (param);
        }
        else
        {
            paramTree.add (param);
        }
    }

    void parseValueType (const juce::XmlElement& item)
    {
        auto vt = new ValueType();
        valueTypes.add (vt);

        vt->name  = item.getStringAttribute ("name");
        vt->label = item.getStringAttribute ("label");

        int curEntry = 0;
        const int numEntries = item.getNumChildElements();

        for (auto* entryXml : item.getChildWithTagNameIterator ("Entry"))
        {
            auto entry = new Entry();
            entry->name = entryXml->getStringAttribute ("name");

            if (entryXml->hasAttribute ("value"))
            {
                entry->range.set (entryXml->getStringAttribute ("value"));
            }
            else
            {
                entry->range.low  = (float) curEntry / (float) numEntries;
                entry->range.high = (float) (curEntry + 1) / (float) numEntries;

                entry->range.inclusiveLow  = true;
                entry->range.inclusiveHigh = (curEntry == numEntries - 1);
            }

            vt->entries.add (entry);
            ++curEntry;
        }
    }

    void parseTemplate (const juce::XmlElement& item)
    {
        auto temp = new Template();
        templates.add (temp);
        temp->name = item.getStringAttribute ("name");

        for (auto* param : item.getChildIterator())
            parseParam (*param, nullptr, temp);
    }

    void parseGroup (const juce::XmlElement& item, Group* parentGroup)
    {
        auto group = new Group();

        if (parentGroup)
        {
            parentGroup->paramTree.add (group);
            group->parent = parentGroup;
        }
        else
        {
            paramTree.add (group);
        }

        group->name = item.getStringAttribute ("name");

        if (item.hasAttribute ("template"))
        {
            juce::StringArray variables;
            variables.addTokens (item.getStringAttribute ("values"), ";", juce::StringRef());
            variables.trim();

            for (auto temp : templates)
            {
                if (temp->name == item.getStringAttribute ("template"))
                {
                    for (int i = 0; i < temp->params.size(); ++i)
                    {
                        auto param = new Param();
                        group->paramTree.add (param);

                        param->parent         = group;
                        param->paramID        = evaluate (temp->params[i]->expr, variables);
                        param->defaultValue   = temp->params[i]->defaultValue;
                        param->label          = temp->params[i]->label;
                        param->name           = temp->params[i]->name;
                        param->numberOfStates = temp->params[i]->numberOfStates;
                        param->shortNames     = temp->params[i]->shortNames;
                        param->type           = temp->params[i]->type;
                    }
                }
            }
        }
        else
        {
            for (auto* subItem : item.getChildIterator())
            {
                if (subItem->hasTagName ("Param"))       parseParam (*subItem, group, nullptr);
                else if (subItem->hasTagName ("Group"))  parseGroup (*subItem, group);
            }
        }
    }

    int evaluate (juce::String expr, const juce::StringArray& variables) const
    {
        juce::StringArray names;
        juce::Array<int> vals;

        for (auto& v : variables)
        {
            if (v.contains ("="))
            {
                names.add (v.upToFirstOccurrenceOf ("=", false, false));
                vals.add  (v.fromFirstOccurrenceOf ("=", false, false).getIntValue());
            }
        }

        for (int i = 0; i < names.size(); ++i)
        {
            for (;;)
            {
                const int idx = expr.indexOfWholeWord (names[i]);
                if (idx < 0)
                    break;

                expr = expr.replaceSection (idx, names[i].length(), juce::String (vals[i]));
            }
        }

        expr = expr.retainCharacters ("01234567890-+")
                   .replace ("+", " + ")
                   .replace ("-", " - ");

        juce::StringArray tokens;
        tokens.addTokens (expr, " ", juce::StringRef());

        bool add = true;
        int val = 0;

        for (const auto& s : tokens)
        {
            if (s == "+")
            {
                add = true;
            }
            else if (s == "-")
            {
                add = false;
            }
            else
            {
                if (add)
                    val += s.getIntValue();
                else
                    val -= s.getIntValue();
            }
        }

        return val;
    }
};

//==============================================================================
struct ModuleHandle final : public ReferenceCountedObject
{
    File file;
    MainCall moduleMain, customMain = {};
    String pluginName;
    std::unique_ptr<XmlElement> vstXml;

    using Ptr = ReferenceCountedObjectPtr<ModuleHandle>;

    static Array<ModuleHandle*>& getActiveModules()
    {
        static Array<ModuleHandle*> activeModules;
        return activeModules;
    }

    //==============================================================================
    static Ptr findOrCreateModule (const File& file)
    {
        for (auto* module : getActiveModules())
            if (module->file == file)
                return module;

        const IdleCallRecursionPreventer icrp;
        shellUIDToCreate = 0;
        _fpreset();

        JUCE_VST_LOG ("Attempting to load VST: " + file.getFullPathName());

        Ptr m = new ModuleHandle (file, nullptr);

        if (m->open())
        {
            _fpreset();
            return m;
        }

        return {};
    }

    //==============================================================================
    ModuleHandle (const File& f, MainCall customMainCall)
        : file (f), moduleMain (customMainCall)
    {
        getActiveModules().add (this);

       #if JUCE_WINDOWS || JUCE_LINUX || JUCE_BSD || JUCE_IOS || JUCE_ANDROID
        fullParentDirectoryPathName = f.getParentDirectory().getFullPathName();
       #elif JUCE_MAC
        FSRef ref;
        makeFSRefFromPath (&ref, f.getParentDirectory().getFullPathName());
        FSGetCatalogInfo (&ref, kFSCatInfoNone, nullptr, nullptr, &parentDirFSSpec, nullptr);
       #endif
    }

    ~ModuleHandle()
    {
        getActiveModules().removeFirstMatchingValue (this);
        close();
    }

    //==============================================================================
   #if ! JUCE_MAC
    String fullParentDirectoryPathName;
   #endif

  #if JUCE_WINDOWS || JUCE_LINUX || JUCE_BSD || JUCE_ANDROID
    DynamicLibrary module;
   #if JUCE_VST2_WINELIB
    /* Win32 PE module handle from LoadLibraryW (separate from `module`
     * which is the dlopen path).  When this is non-null the plugin was
     * loaded as a Windows PE binary via Wine; close() must FreeLibrary
     * it instead of letting DynamicLibrary's dlclose run. */
    void* peModule = nullptr;
   #endif

    bool open()
    {
        if (moduleMain != nullptr)
            return true;

        pluginName = file.getFileNameWithoutExtension();

       #if JUCE_VST2_WINELIB
        /* Detect Windows PE binaries by their "MZ" magic and load them
         * via LoadLibraryW.  Linux-native .so files fall through to the
         * dlopen path below.  Single PE load per DLL — globalWineDispatcher
         * ensures DllMain runs on a Win32 thread (PROCESS_ATTACH may
         * itself spawn boost::thread RT workers, which need an RT-capable
         * parent — see yabridge::nspa::set_thread_time_critical).  Per-
         * instance dispatchers (one per VSTPluginInstanceHeadless) carry
         * the per-instance state from here onwards. */
        if (winelib_is_pe_file (file))
        {
            const String dosPath = winelib_unix_to_dos_path (file.getFullPathName());
            winelib_log ("[vst2:ModuleHandle::open] DOS path '%s'", dosPath.toRawUTF8());
            const auto widePath = winelib_path_to_utf16 (dosPath);

            globalWineDispatcher().run ([&] { peModule = LoadLibraryW (widePath.getData()); });
            winelib_log ("[vst2:ModuleHandle::open] LoadLibraryW -> %p", peModule);

            if (peModule != nullptr)
            {
                moduleMain = (MainCall) GetProcAddress (peModule, "VSTPluginMain");
                if (moduleMain == nullptr)
                    moduleMain = (MainCall) GetProcAddress (peModule, "main");

                winelib_log ("[vst2:ModuleHandle::open] entry point '%s' -> %p",
                             moduleMain ? "found" : "missing", (void*) moduleMain);

                if (moduleMain == nullptr)
                {
                    FreeLibrary (peModule);
                    peModule = nullptr;
                    return false;
                }

                vstXml = parseXML (file.withFileExtension ("vstxml"));
                return true;
            }

            return false;
        }

        /* Non-PE file under winelib: refuse cleanly.  JUCE's default
         * Linux VST2 search paths (/usr/lib/vst, /usr/local/lib/vst,
         * ~/.vst) often contain Linux-native `.so` plugins (Carla's
         * fake-VST bundles like CarlaPatchbayFX.so are the canonical
         * offender).  Falling through to module.open() below would
         * dlopen them, which crashes the scanner on bundles that lie
         * about their entry points.  A winelib Element host only
         * makes sense for Windows PE plugins anyway. */
        return false;
       #endif

        module.open (file.getFullPathName());

        moduleMain = (MainCall) module.getFunction ("VSTPluginMain");

        if (moduleMain == nullptr)
            moduleMain = (MainCall) module.getFunction ("main");

        JUCE_VST_WRAPPER_LOAD_CUSTOM_MAIN

        if (moduleMain != nullptr)
        {
            vstXml = parseXML (file.withFileExtension ("vstxml"));

           #if JUCE_WINDOWS
            if (vstXml == nullptr)
                vstXml = parseXML (getDLLResource (file, "VSTXML", 1));
           #endif
        }

        return moduleMain != nullptr;
    }

    void close()
    {
        _fpreset(); // (doesn't do any harm)

       #if JUCE_VST2_WINELIB
        if (peModule != nullptr)
        {
            globalWineDispatcher().run ([&] { FreeLibrary (peModule); });
            peModule = nullptr;
            return;
        }
       #endif

        module.close();
    }

    void closeEffect (Vst2::AEffect* eff)
    {
       #if JUCE_VST2_WINELIB
        /* effClose is a plugin call — must run on the per-instance
         * Win32 worker for thread-identity consistency with everything
         * else the instance dispatched.  The instance owns the worker
         * and dispatches its own effClose via its destructor path
         * (see VSTPluginInstanceHeadless::cleanup), so here we just
         * skip — the dispatcher has already done it.  Reached only on
         * the non-Wine path when called from the standalone code path
         * that does not own a per-instance dispatcher. */
        if (peModule != nullptr)
            return;
       #endif
        eff->dispatcher (eff, Vst2::effClose, 0, 0, nullptr, 0);
    }

   #if JUCE_WINDOWS
    static String getDLLResource (const File& dllFile, const String& type, int resID)
    {
        DynamicLibrary dll (dllFile.getFullPathName());
        auto dllModule = (HMODULE) dll.getNativeHandle();

        if (dllModule != INVALID_HANDLE_VALUE)
        {
            if (auto res = FindResource (dllModule, MAKEINTRESOURCE (resID), type.toWideCharPointer()))
            {
                if (auto hGlob = LoadResource (dllModule, res))
                {
                    auto* data = static_cast<const char*> (LockResource (hGlob));
                    return String::fromUTF8 (data, (int) SizeofResource (dllModule, res));
                }
            }
        }

        return {};
    }
   #endif
  #else
    Handle resHandle = {};
    CFUniquePtr<CFBundleRef> bundleRef;

   #if JUCE_MAC
    CFBundleRefNum resFileId = {};
    FSSpec parentDirFSSpec;
   #endif

    bool open()
    {
        if (moduleMain != nullptr)
            return true;

        bool ok = false;

        if (file.hasFileExtension (".vst"))
        {
            auto* utf8 = file.getFullPathName().toRawUTF8();

            if (auto url = CFUniquePtr<CFURLRef> (CFURLCreateFromFileSystemRepresentation (nullptr, (const UInt8*) utf8,
                                                                                           (CFIndex) strlen (utf8), file.isDirectory())))
            {
                bundleRef.reset (CFBundleCreate (kCFAllocatorDefault, url.get()));

                if (bundleRef != nullptr)
                {
                    if (CFBundleLoadExecutable (bundleRef.get()))
                    {
                        moduleMain = (MainCall) CFBundleGetFunctionPointerForName (bundleRef.get(), CFSTR ("main_macho"));

                        if (moduleMain == nullptr)
                            moduleMain = (MainCall) CFBundleGetFunctionPointerForName (bundleRef.get(), CFSTR ("VSTPluginMain"));

                        JUCE_VST_WRAPPER_LOAD_CUSTOM_MAIN

                        if (moduleMain != nullptr)
                        {
                            if (CFTypeRef name = CFBundleGetValueForInfoDictionaryKey (bundleRef.get(), CFSTR ("CFBundleName")))
                            {
                                if (CFGetTypeID (name) == CFStringGetTypeID())
                                {
                                    char buffer[1024];

                                    if (CFStringGetCString ((CFStringRef) name, buffer, sizeof (buffer), CFStringGetSystemEncoding()))
                                        pluginName = buffer;
                                }
                            }

                            if (pluginName.isEmpty())
                                pluginName = file.getFileNameWithoutExtension();

                           #if JUCE_MAC
                            resFileId = CFBundleOpenBundleResourceMap (bundleRef.get());
                           #endif

                            ok = true;

                            auto vstXmlFiles = file
                                                   #if JUCE_MAC
                                                    .getChildFile ("Contents")
                                                    .getChildFile ("Resources")
                                                   #endif
                                                    .findChildFiles (File::findFiles, false, "*.vstxml");

                            if (! vstXmlFiles.isEmpty())
                                vstXml = parseXML (vstXmlFiles.getReference (0));
                        }
                    }

                    if (! ok)
                    {
                        CFBundleUnloadExecutable (bundleRef.get());
                        bundleRef = nullptr;
                    }
                }
            }
        }

        return ok;
    }

    void close()
    {
        if (bundleRef != nullptr)
        {
           #if JUCE_MAC
            CFBundleCloseBundleResourceMap (bundleRef.get(), resFileId);
           #endif

            if (CFGetRetainCount (bundleRef.get()) == 1)
                CFBundleUnloadExecutable (bundleRef.get());

            if (CFGetRetainCount (bundleRef.get()) > 0)
                bundleRef = nullptr;
        }
    }

    void closeEffect (Vst2::AEffect* eff)
    {
        eff->dispatcher (eff, Vst2::effClose, 0, 0, nullptr, 0);
    }

  #endif

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModuleHandle)
};

static const int defaultVSTSampleRateValue = 44100;
static const int defaultVSTBlockSizeValue = 512;

class TempChannelPointers
{
public:
    template <typename T>
    auto getArrayOfModifiableWritePointers (AudioBuffer<T>& buffer)
    {
        auto& pointers = getPointers (Tag<T>{});

        jassert (buffer.getNumChannels() <= static_cast<int> (pointers.capacity()));
        pointers.resize (jmax (pointers.size(), (size_t) buffer.getNumChannels()));

        std::copy (buffer.getArrayOfWritePointers(),
                   buffer.getArrayOfWritePointers() + buffer.getNumChannels(),
                   pointers.begin());

        return pointers.data();
    }

private:
    template <typename> struct Tag {};

    auto& getPointers (Tag<float>)  { return floatPointers; }
    auto& getPointers (Tag<double>) { return doublePointers; }

    std::vector<float*>  floatPointers  { 128 };
    std::vector<double*> doublePointers { 128 };
};

//==============================================================================
struct VSTPluginInstanceHeadless : public AudioPluginInstance
{
    struct VSTParameter final   : public Parameter
    {
        VSTParameter (VSTPluginInstanceHeadless& parent,
                      const String& paramName,
                      const Array<String>& shortParamNames,
                      float paramDefaultValue,
                      const String& paramLabel,
                      bool paramIsAutomatable,
                      bool paramIsDiscrete,
                      int numParamSteps,
                      bool isBoolSwitch,
                      const StringArray& paramValueStrings,
                      const VSTXMLInfo::ValueType* paramValueType)
            : pluginInstance (parent),
              name (paramName),
              shortNames (shortParamNames),
              defaultValue (paramDefaultValue),
              label (paramLabel),
              automatable (paramIsAutomatable),
              discrete (paramIsDiscrete),
              numSteps (numParamSteps),
              isSwitch (isBoolSwitch),
              vstValueStrings (paramValueStrings),
              valueType (paramValueType)
        {
        }

        float getValue() const override
        {
            if (auto* effect = pluginInstance.vstEffect)
            {
                const ScopedLock sl (pluginInstance.lock);

                return effect->getParameter (effect, getParameterIndex());
            }

            return 0.0f;
        }

        void setValue (float newValue) override
        {
            if (auto* effect = pluginInstance.vstEffect)
            {
                const ScopedLock sl (pluginInstance.lock);

                if (! approximatelyEqual (effect->getParameter (effect, getParameterIndex()), newValue))
                    effect->setParameter (effect, getParameterIndex(), newValue);
            }
        }

        String getText (float value, int maximumStringLength) const override
        {
            if (valueType != nullptr)
            {
                for (auto& v : valueType->entries)
                    if (v->range.contains (value))
                        return v->name;
            }

            return Parameter::getText (value, maximumStringLength);
        }

        float getValueForText (const String& text) const override
        {
            if (valueType != nullptr)
            {
                for (auto& v : valueType->entries)
                    if (v->name == text)
                        return (v->range.high + v->range.low) / 2.0f;
            }

            return Parameter::getValueForText (text);
        }

        String getCurrentValueAsText() const override
        {
            if (valueType != nullptr || ! vstValueStrings.isEmpty())
                return getText (getValue(), 1024);

            return pluginInstance.getTextForOpcode (getParameterIndex(), Vst2::effGetParamDisplay);
        }

        float getDefaultValue() const override
        {
            return defaultValue;
        }

        String getName (int maximumStringLength) const override
        {
            if (name.isEmpty())
                return pluginInstance.getTextForOpcode (getParameterIndex(),
                                                        Vst2::effGetParamName);

            if (name.length() <= maximumStringLength)
                return name;

            if (! shortNames.isEmpty())
            {
                for (auto& n : shortNames)
                    if (n.length() <= maximumStringLength)
                        return n;

                return shortNames.getLast();
            }

            return name;
        }

        String getLabel() const override
        {
            return label.isEmpty() ? pluginInstance.getTextForOpcode (getParameterIndex(),
                                                                      Vst2::effGetParamLabel)
                                   : label;
        }

        bool isAutomatable() const override
        {
            return automatable;
        }

        bool isDiscrete() const override
        {
            return discrete;
        }

        bool isBoolean() const override
        {
            return isSwitch;
        }

        int getNumSteps() const override
        {
            return numSteps;
        }

        StringArray getAllValueStrings() const override
        {
            return vstValueStrings;
        }

        String getParameterID() const override
        {
            return String (getParameterIndex());
        }

        VSTPluginInstanceHeadless& pluginInstance;

        const String name;
        const Array<String> shortNames;
        const float defaultValue;
        const String label;
        const bool automatable, discrete;
        const int numSteps;
        const bool isSwitch;
        const StringArray vstValueStrings;
        const VSTXMLInfo::ValueType* const valueType;
    };

    VSTPluginInstanceHeadless (const ModuleHandle::Ptr& mh,
                               const BusesProperties& ioConfig,
                               Vst2::AEffect* effect,
                               double sampleRateToUse,
                               int blockSizeToUse)
        : AudioPluginInstance (ioConfig),
          vstEffect (effect),
          vstModule (mh),
          name (mh->pluginName),
          bypassParam (new VST2BypassParameter (*this))
    {
        jassert (vstEffect != nullptr);

        if (auto* xml = vstModule->vstXml.get())
            xmlInfo.reset (VSTXMLInfo::createFor (*xml));

        refreshParameterList();

        vstSupportsBypass = (pluginCanDo ("bypass") > 0);
        setRateAndBufferSizeDetails (sampleRateToUse, blockSizeToUse);
    }

    void refreshParameterList() override
    {
        AudioProcessorParameterGroup newParameterTree;

        for (int i = 0; i < vstEffect->numParams; ++i)
        {
            String paramName;
            Array<String> shortParamNames;
            float defaultValue = 0;
            String label;
            bool isAutomatable = dispatch (Vst2::effCanBeAutomated, i, 0, nullptr, 0) != 0;
            bool isDiscrete = false;
            int numSteps = AudioProcessor::getDefaultNumParameterSteps();
            bool isBoolSwitch = false;
            StringArray parameterValueStrings;
            const VSTXMLInfo::ValueType* valueType = nullptr;

            if (xmlInfo != nullptr)
            {
                if (auto* param = xmlInfo->getParamForID (i, nullptr))
                {
                    paramName = param->name;

                    for (auto& n : param->shortNames)
                        shortParamNames.add (n);

                    struct LengthComparator
                    {
                        static int compareElements (const juce::String& first, const juce::String& second) noexcept
                        {
                            return first.length() - second.length();
                        }
                    };

                    LengthComparator comp;
                    shortParamNames.sort (comp);

                    defaultValue = param->defaultValue;
                    label = param->label;

                    if (param->type == "switch")
                    {
                        isBoolSwitch = true;
                        numSteps = 2;
                        valueType = &xmlInfo->switchValueType;
                    }
                    else
                    {
                        valueType = xmlInfo->getValueType (param->type);
                    }

                    if (param->numberOfStates >= 2)
                    {
                        numSteps = param->numberOfStates;

                        if (valueType != nullptr)
                        {
                            for (auto* entry : valueType->entries)
                                parameterValueStrings.add (entry->name);

                            parameterValueStrings.removeEmptyStrings();
                        }
                    }

                    isDiscrete = (numSteps != AudioProcessor::getDefaultNumParameterSteps());
                }
            }

            newParameterTree.addChild (std::make_unique<VSTParameter> (*this, paramName, shortParamNames, defaultValue,
                                                                       label, isAutomatable, isDiscrete, numSteps,
                                                                       isBoolSwitch, parameterValueStrings, valueType));
        }

        setHostedParameterTree (std::move (newParameterTree));
    }

   #if JUCE_VST2_WINELIB
    /* Per-instance Win32 worker — see Phase 4 VST3 commit fc363bb +
     * juce_VST3PluginFormatImpl.h for the same pattern.  Allocated by
     * `create()` BEFORE the AEffect is built (so `moduleMain`, the
     * VST2 factory call, runs on this worker), then transferred into
     * the constructed instance.  Every per-instance plugin call
     * (`vstEffect->dispatcher(...)`, `processReplacing`, etc.) goes
     * through `wineDispatcher->run(...)`, giving the plugin one
     * stable Win32 thread identity for its whole lifetime.
     *
     * Lifetime: outlives `cleanup()`'s effClose dispatch (and any
     * plugin-internal teardown it triggers) because it's destructed
     * AFTER cleanup() returns in `~VSTPluginInstanceHeadless`.
     */
    std::unique_ptr<WineWin32Dispatcher> wineDispatcher;
   #endif

    ~VSTPluginInstanceHeadless() override
    {
        if (vstEffect != nullptr && vstEffect->magic == 0x56737450 /* 'VstP' */)
            MessageManager::callSync ([this] { cleanup(); });
    }

    void cleanup()
    {
        if (vstEffect != nullptr && vstEffect->magic == 0x56737450 /* 'VstP' */)
        {
           #if JUCE_MAC
            if (vstModule->resFileId != 0)
                UseResFile (vstModule->resFileId);
           #endif

            // Must delete any editors before deleting the plugin instance!
            jassert (getActiveEditor() == nullptr);

            _fpreset(); // some dodgy plug-ins mess around with this

           #if JUCE_VST2_WINELIB
            /* effClose on the per-instance worker so the plugin sees
             * the same Win32 thread identity it had through init +
             * audio.  vstModule->closeEffect is a no-op under
             * JUCE_VST2_WINELIB (it sees peModule != nullptr and
             * returns); we dispatch the close call here ourselves. */
            if (wineDispatcher != nullptr)
            {
                wineDispatcher->run ([&]
                {
                    try
                    {
                        winelib_vst2_abi::callDispatcher (vstEffect, Vst2::effClose, 0, 0, nullptr, 0);
                    }
                    catch (...) {}
                });
            }
            else
           #endif
            {
                vstModule->closeEffect (vstEffect);
            }
        }

        vstModule = nullptr;
        vstEffect = nullptr;
    }

    template <typename TypeToCreate>
    static std::unique_ptr<TypeToCreate> create (const ModuleHandle::Ptr& newModule,
                                                 double initialSampleRate,
                                                 int initialBlockSize)
    {
       #if JUCE_VST2_WINELIB
        /* Allocate the per-instance Win32 worker BEFORE the AEffect is
         * built — the plugin's factory entry (`moduleMain`, invoked
         * inside `constructEffect`) returns the AEffect, and that call
         * MUST happen on the same Win32 thread that later runs every
         * `dispatcher(opcode, ...)` and `processReplacing` call.
         * Plugins (u-he in particular) stash internal CRITICAL_SECTION
         * owner state during the factory call; calling later with a
         * different Win32 thread would trip thread-affinity assertions
         * and ExitProcess.  See Phase 4 VST3 commit fc363bb for the
         * identical pattern on the VST3 side. */
        auto wineDisp = std::make_unique<WineWin32Dispatcher>();

        Vst2::AEffect* newEffect = nullptr;
        wineDisp->run ([&] { newEffect = constructEffect (newModule); });

        if (newEffect != nullptr)
        {
            newEffect->resvd2 = 0;
            auto blockSize = jmax (32, initialBlockSize);

            /* All four init opcodes run as a single dispatched unit so
             * the plugin's state-machine progresses on one stable Win32
             * context with no inter-call jitter through the worker's
             * task queue. */
            BusesProperties ioConfig;
            wineDisp->run ([&]
            {
                try
                {
                    winelib_vst2_abi::callDispatcher (newEffect, Vst2::effIdentify, 0, 0, nullptr, 0);
                    winelib_vst2_abi::callDispatcher (newEffect, Vst2::effSetSampleRate, 0, 0, nullptr, static_cast<float> (initialSampleRate));
                    winelib_vst2_abi::callDispatcher (newEffect, Vst2::effSetBlockSize,  0, blockSize, nullptr, 0);
                    winelib_vst2_abi::callDispatcher (newEffect, Vst2::effOpen, 0, 0, nullptr, 0);
                    ioConfig = queryBusIO (newEffect);
                }
                catch (...)
                {
                    // Plugin init threw — leave newEffect partially
                    // initialised but contained; the host will either
                    // log a "failed to initialise" and discard, or
                    // teardown via cleanup() which dispatches effClose
                    // through the same worker.
                }
            });

            auto instance = std::make_unique<TypeToCreate> (newModule, ioConfig, newEffect, initialSampleRate, blockSize);
            instance->wineDispatcher = std::move (wineDisp);
            return instance;
        }

        return nullptr;
       #else
        if (auto* newEffect = constructEffect (newModule))
        {
            newEffect->resvd2 = 0;

            newEffect->dispatcher (newEffect, Vst2::effIdentify, 0, 0, nullptr, 0);

            auto blockSize = jmax (32, initialBlockSize);

            newEffect->dispatcher (newEffect, Vst2::effSetSampleRate, 0, 0, nullptr, static_cast<float> (initialSampleRate));
            newEffect->dispatcher (newEffect, Vst2::effSetBlockSize,  0, blockSize, nullptr, 0);

            newEffect->dispatcher (newEffect, Vst2::effOpen, 0, 0, nullptr, 0);
            BusesProperties ioConfig = queryBusIO (newEffect);

            return std::make_unique<TypeToCreate> (newModule, ioConfig, newEffect, initialSampleRate, blockSize);
        }

        return nullptr;
       #endif
    }

    //==============================================================================
    void fillInPluginDescription (PluginDescription& desc) const override
    {
        desc.name = name;

        {
            char buffer[512] = { 0 };
            dispatch (Vst2::effGetEffectName, 0, 0, buffer, 0);

            desc.descriptiveName = String::createStringFromData (buffer, (int) sizeof (buffer)).trim();

            if (desc.descriptiveName.isEmpty())
                desc.descriptiveName = name;
        }

        desc.fileOrIdentifier = vstModule->file.getFullPathName();
        desc.uniqueId = desc.deprecatedUid = getUID();
        desc.lastFileModTime = vstModule->file.getLastModificationTime();
        desc.lastInfoUpdateTime = Time::getCurrentTime();
        desc.pluginFormatName = "VST";
        desc.category = getCategory();

        {
            char buffer[512] = { 0 };
            dispatch (Vst2::effGetVendorString, 0, 0, buffer, 0);
            desc.manufacturerName = String::createStringFromData (buffer, (int) sizeof (buffer)).trim();
        }

        desc.version = getVersion();
        desc.numInputChannels = getTotalNumInputChannels();
        desc.numOutputChannels = getTotalNumOutputChannels();
        desc.isInstrument = isSynthPlugin();
    }

    bool initialiseEffect (double initialSampleRate, int initialBlockSize)
    {
        if (vstEffect != nullptr)
        {
            vstEffect->resvd2 = (pointer_sized_int) (pointer_sized_int) this;
            initialise (initialSampleRate, initialBlockSize);
            return true;
        }

        return false;
    }

    void initialise (double initialSampleRate, int initialBlockSize)
    {
        if (initialised || vstEffect == nullptr)
            return;

       #if JUCE_WINDOWS
        // On Windows it's highly advisable to create your plugins using the message thread,
        // because many plugins need a chance to create HWNDs that will get their
        // messages delivered by the main message thread, and that's not possible from
        // a background thread.
        JUCE_ASSERT_MESSAGE_THREAD
       #endif

        JUCE_VST_LOG ("Initialising VST: " + vstModule->pluginName + " (" + getVersion() + ")");
        initialised = true;

        setRateAndBufferSizeDetails (initialSampleRate, initialBlockSize);

        dispatch (Vst2::effIdentify, 0, 0, nullptr, 0);

        if (getSampleRate() > 0)
            dispatch (Vst2::effSetSampleRate, 0, 0, nullptr, (float) getSampleRate());

        if (getBlockSize() > 0)
            dispatch (Vst2::effSetBlockSize, 0, jmax (32, getBlockSize()), nullptr, 0);

        dispatch (Vst2::effOpen, 0, 0, nullptr, 0);

        setRateAndBufferSizeDetails (getSampleRate(), getBlockSize());

        if (getNumPrograms() > 1)
            setCurrentProgram (0);
        else
            dispatch (Vst2::effSetProgram, 0, 0, nullptr, 0);

        for (int i = vstEffect->numInputs;  --i >= 0;)  dispatch (Vst2::effConnectInput,  i, 1, nullptr, 0);
        for (int i = vstEffect->numOutputs; --i >= 0;)  dispatch (Vst2::effConnectOutput, i, 1, nullptr, 0);

        if (getVstCategory() != Vst2::kPlugCategShell) // (workaround for Waves 5 plugins which crash during this call)
            updateStoredProgramNames();

        wantsMidiMessages = pluginCanDo ("receiveVstMidiEvent") > 0 || isSynthPlugin();

        setLatencySamples (vstEffect->initialDelay);
    }

    void getExtensions (ExtensionsVisitor& visitor) const override
    {
        struct Extensions final : public ExtensionsVisitor::VSTClient
        {
            explicit Extensions (const VSTPluginInstanceHeadless* instanceIn) : instance (instanceIn) {}

            AEffect* getAEffectPtr() const noexcept override   { return reinterpret_cast<AEffect*> (instance->vstEffect); }

            const VSTPluginInstanceHeadless* instance = nullptr;
        };

        visitor.visitVSTClient (Extensions { this });
    }

    void* getPlatformSpecificData() override    { return vstEffect; }

    const String getName() const override
    {
        if (vstEffect != nullptr)
        {
            char buffer[512] = { 0 };

            if (dispatch (Vst2::effGetProductString, 0, 0, buffer, 0) != 0)
            {
                String productName = String::createStringFromData (buffer, (int) sizeof (buffer));

                if (productName.isNotEmpty())
                    return productName;
            }
        }

        return name;
    }

    int getUID() const
    {
        int uid = vstEffect != nullptr ? vstEffect->uniqueID : 0;

        if (uid == 0)
            uid = vstModule->file.hashCode();

        return uid;
    }

    double getTailLengthSeconds() const override
    {
        if (vstEffect == nullptr)
            return 0.0;

        if ((vstEffect->flags & Vst2::effFlagsNoSoundInStop) != 0)
            return 0.0;

        auto tailSize = dispatch (Vst2::effGetTailSize, 0, 0, nullptr, 0);
        auto sampleRate = getSampleRate();

        // remain backward compatible with old JUCE plug-ins: anything larger
        // than INT32_MAX is an invalid tail time but old JUCE 64-bit plug-ins
        // would return INT64_MAX for infinite tail time. So treat anything
        // equal or greater than INT32_MAX as infinite tail time.
        if (tailSize >= std::numeric_limits<int32>::max())
            return std::numeric_limits<double>::infinity();

        if (tailSize >= 0 && sampleRate > 0)
            return static_cast<double> (tailSize) / sampleRate;

        return 0.0;
    }

    bool acceptsMidi() const override    { return wantsMidiMessages; }
    bool producesMidi() const override   { return pluginCanDo ("sendVstMidiEvent") > 0; }
    bool supportsMPE() const override    { return pluginCanDo ("MPE") > 0; }

    Vst2::VstPlugCategory getVstCategory() const noexcept     { return (Vst2::VstPlugCategory) dispatch (Vst2::effGetPlugCategory, 0, 0, nullptr, 0); }

    bool isSynthPlugin() const  { return (vstEffect != nullptr && (vstEffect->flags & Vst2::effFlagsIsSynth) != 0); }

    int pluginCanDo (const char* text) const  { return (int) dispatch (Vst2::effCanDo, 0, 0, (void*) text,  0); }

    std::optional<String> getNameForMidiNoteNumber (int note, int midiChannel) override
    {
        Vst2::MidiKeyName keyName{};

        keyName.thisProgramIndex = getCurrentProgram();
        keyName.thisKeyNumber = note;

        return dispatch (Vst2::effGetMidiKeyName, midiChannel, 0, &keyName, 0.0f) != 0
             ? std::make_optional (String::createStringFromData (keyName.keyName, Vst2::kVstMaxNameLen))
             : std::nullopt;
    }

    //==============================================================================
    void prepareToPlay (double rate, int samplesPerBlockExpected) override
    {
        auto numInputBuses  = getBusCount (true);
        auto numOutputBuses = getBusCount (false);

        setRateAndBufferSizeDetails (rate, samplesPerBlockExpected);

        if (numInputBuses <= 1 && numOutputBuses <= 1)
        {
            SpeakerMappings::VstSpeakerConfigurationHolder inArr  (getChannelLayoutOfBus (true,  0));
            SpeakerMappings::VstSpeakerConfigurationHolder outArr (getChannelLayoutOfBus (false, 0));

            dispatch (Vst2::effSetSpeakerArrangement, 0, (pointer_sized_int) &inArr.get(), (void*) &outArr.get(), 0.0f);
        }

        vstHostTime.tempo = 120.0;
        vstHostTime.timeSigNumerator = 4;
        vstHostTime.timeSigDenominator = 4;
        vstHostTime.sampleRate = rate;
        vstHostTime.samplePos = 0;
        vstHostTime.flags = Vst2::kVstNanosValid
                              | Vst2::kVstAutomationWriting
                              | Vst2::kVstAutomationReading;

        initialise (rate, samplesPerBlockExpected);

        if (initialised)
        {
            wantsMidiMessages = wantsMidiMessages || (pluginCanDo ("receiveVstMidiEvent") > 0) || isSynthPlugin();

            if (wantsMidiMessages)
                midiEventsToSend.ensureSize (256);
            else
                midiEventsToSend.freeEvents();

            incomingMidi.clear();

            dispatch (Vst2::effSetSampleRate, 0, 0, nullptr, (float) rate);
            dispatch (Vst2::effSetBlockSize, 0, jmax (16, samplesPerBlockExpected), nullptr, 0);

            if (supportsDoublePrecisionProcessing())
            {
                int32 vstPrecision = isUsingDoublePrecision() ? Vst2::kVstProcessPrecision64
                                                              : Vst2::kVstProcessPrecision32;

                dispatch (Vst2::effSetProcessPrecision, 0, (pointer_sized_int) vstPrecision, nullptr, 0);
            }

            auto maxChannels = jmax (1, jmax (vstEffect->numInputs, vstEffect->numOutputs));

            tmpBufferFloat .setSize (maxChannels, samplesPerBlockExpected);
            tmpBufferDouble.setSize (maxChannels, samplesPerBlockExpected);

            channelBufferFloat .calloc (static_cast<size_t> (maxChannels));
            channelBufferDouble.calloc (static_cast<size_t> (maxChannels));

            outOfPlaceBuffer.setSize (jmax (1, vstEffect->numOutputs), samplesPerBlockExpected);

            if (! isPowerOn)
                setPower (true);

            // dodgy hack to force some plugins to initialise the sample rate
            if (! hasEditor())
            {
                if (auto* firstParam = getParameters()[0])
                {
                    auto old = firstParam->getValue();
                    firstParam->setValue ((old < 0.5f) ? 1.0f : 0.0f);
                    firstParam->setValue (old);
                }
            }

            dispatch (Vst2::effStartProcess, 0, 0, nullptr, 0);

            setLatencySamples (vstEffect->initialDelay);
        }
    }

    void releaseResources() override
    {
        if (initialised)
        {
            dispatch (Vst2::effStopProcess, 0, 0, nullptr, 0);
            setPower (false);
        }

        channelBufferFloat.free();
        tmpBufferFloat.setSize (0, 0);

        channelBufferDouble.free();
        tmpBufferDouble.setSize (0, 0);

        outOfPlaceBuffer.setSize (1, 1);
        incomingMidi.clear();

        midiEventsToSend.freeEvents();
    }

    void reset() override
    {
        if (isPowerOn)
        {
            setPower (false);
            setPower (true);
        }
    }

    //==============================================================================
    void processBlock (AudioBuffer<float>& buffer, MidiBuffer& midiMessages) override
    {
        jassert (! isUsingDoublePrecision());
        processAudio (buffer, midiMessages, tmpBufferFloat, channelBufferFloat, false);
    }

    void processBlock (AudioBuffer<double>& buffer, MidiBuffer& midiMessages) override
    {
        jassert (isUsingDoublePrecision());
        processAudio (buffer, midiMessages, tmpBufferDouble, channelBufferDouble, false);
    }

    void processBlockBypassed (AudioBuffer<float>& buffer, MidiBuffer& midiMessages) override
    {
        jassert (! isUsingDoublePrecision());
        processAudio (buffer, midiMessages, tmpBufferFloat, channelBufferFloat, true);
    }

    void processBlockBypassed (AudioBuffer<double>& buffer, MidiBuffer& midiMessages) override
    {
        jassert (isUsingDoublePrecision());
        processAudio (buffer, midiMessages, tmpBufferDouble, channelBufferDouble, true);
    }

    //==============================================================================
    bool supportsDoublePrecisionProcessing() const override
    {
        return ((vstEffect->flags & Vst2::effFlagsCanReplacing) != 0
             && (vstEffect->flags & Vst2::effFlagsCanDoubleReplacing) != 0);
    }

    AudioProcessorParameter* getBypassParameter() const override               { return vstSupportsBypass ? bypassParam.get() : nullptr; }

    //==============================================================================
    bool canAddBus (bool) const override                                       { return false; }
    bool canRemoveBus (bool) const override                                    { return false; }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        auto numInputBuses  = getBusCount (true);
        auto numOutputBuses = getBusCount (false);

        // it's not possible to change layout if there are sidechains/aux buses
        if (numInputBuses > 1 || numOutputBuses > 1)
            return (layouts == getBusesLayout());

        return (layouts.getNumChannels (true,  0) <= vstEffect->numInputs
             && layouts.getNumChannels (false, 0) <= vstEffect->numOutputs);
    }

    //==============================================================================
    bool hasEditor() const override                  { return false; }
    AudioProcessorEditor* createEditor() override    { return nullptr;}

    //==============================================================================
    const String getInputChannelName (int index) const override
    {
        if (isValidChannel (index, true))
        {
            Vst2::VstPinProperties pinProps;
            if (dispatch (Vst2::effGetInputProperties, index, 0, &pinProps, 0.0f) != 0)
                return String (pinProps.label, sizeof (pinProps.label));
        }

        return {};
    }

    bool isInputChannelStereoPair (int index) const override
    {
        if (! isValidChannel (index, true))
            return false;

        Vst2::VstPinProperties pinProps;
        if (dispatch (Vst2::effGetInputProperties, index, 0, &pinProps, 0.0f) != 0)
            return (pinProps.flags & Vst2::kVstPinIsStereo) != 0;

        return true;
    }

    const String getOutputChannelName (int index) const override
    {
        if (isValidChannel (index, false))
        {
            Vst2::VstPinProperties pinProps;
            if (dispatch (Vst2::effGetOutputProperties, index, 0, &pinProps, 0.0f) != 0)
                return String (pinProps.label, sizeof (pinProps.label));
        }

        return {};
    }

    bool isOutputChannelStereoPair (int index) const override
    {
        if (! isValidChannel (index, false))
            return false;

        Vst2::VstPinProperties pinProps;
        if (dispatch (Vst2::effGetOutputProperties, index, 0, &pinProps, 0.0f) != 0)
            return (pinProps.flags & Vst2::kVstPinIsStereo) != 0;

        return true;
    }

    bool isValidChannel (int index, bool isInput) const noexcept
    {
        return isPositiveAndBelow (index, isInput ? getTotalNumInputChannels()
                                                  : getTotalNumOutputChannels());
    }

    //==============================================================================
    int getNumPrograms() override          { return vstEffect != nullptr ? jmax (0, vstEffect->numPrograms) : 0; }

    // NB: some plugs return negative numbers from this function.
    int getCurrentProgram() override       { return (int) dispatch (Vst2::effGetProgram, 0, 0, nullptr, 0); }

    void setCurrentProgram (int newIndex) override
    {
        if (getNumPrograms() > 0 && newIndex != getCurrentProgram())
            dispatch (Vst2::effSetProgram, 0, jlimit (0, getNumPrograms() - 1, newIndex), nullptr, 0);
    }

    const String getProgramName (int index) override
    {
        if (index >= 0)
        {
            if (index == getCurrentProgram())
                return getCurrentProgramName();

            if (vstEffect != nullptr)
            {
                char nm[264] = { 0 };

                if (dispatch (Vst2::effGetProgramNameIndexed, jlimit (0, getNumPrograms() - 1, index), -1, nm, 0) != 0)
                    return String::fromUTF8 (nm).trim();
            }
        }

        return {};
    }

    void changeProgramName (int index, const String& newName) override
    {
        if (index >= 0 && index == getCurrentProgram())
        {
            if (getNumPrograms() > 0 && newName != getCurrentProgramName())
                dispatch (Vst2::effSetProgramName, 0, 0, (void*) newName.substring (0, 24).toRawUTF8(), 0.0f);
        }
        else
        {
            jassertfalse; // xxx not implemented!
        }
    }

    //==============================================================================
    void getStateInformation (MemoryBlock& mb) override                  { saveToFXBFile (mb, true); }
    void getCurrentProgramStateInformation (MemoryBlock& mb) override    { saveToFXBFile (mb, false); }

    void setStateInformation (const void* data, int size) override               { loadFromFXBFile (data, (size_t) size); }
    void setCurrentProgramStateInformation (const void* data, int size) override { loadFromFXBFile (data, (size_t) size); }

    //==============================================================================
    pointer_sized_int handleCallback (int32 opcode, int32 index, pointer_sized_int value, void* ptr, float opt)
    {
        switch (opcode)
        {
            case Vst2::audioMasterAutomate:
                if (auto* param = getParameters()[index])
                    param->sendValueChangedMessageToListeners (opt);
                else
                    jassertfalse; // Invalid parameter index!

                break;

            case Vst2::audioMasterProcessEvents:            handleMidiFromPlugin ((const Vst2::VstEvents*) ptr); break;
            case Vst2::audioMasterGetTime:                  return getVSTTime();
            case Vst2::audioMasterIdle:                     handleIdle(); break;
            case Vst2::audioMasterSizeWindow:               setWindowSize (index, (int) value); return 1;
            case Vst2::audioMasterUpdateDisplay:            updateDisplay(); break;
            case Vst2::audioMasterIOChanged:                setLatencySamples (vstEffect->initialDelay); break;
            case Vst2::audioMasterNeedIdle:                 needIdle(); break;

            case Vst2::audioMasterGetSampleRate:            return (pointer_sized_int) (getSampleRate() > 0 ? getSampleRate() : defaultVSTSampleRateValue);
            case Vst2::audioMasterGetBlockSize:             return (pointer_sized_int) (getBlockSize() > 0  ? getBlockSize()  : defaultVSTBlockSizeValue);
            case Vst2::audioMasterWantMidi:                 wantsMidiMessages = true; break;
            case Vst2::audioMasterGetDirectory:             return getVstDirectory();

            case Vst2::audioMasterTempoAt:                  return (pointer_sized_int) (extraFunctions != nullptr ? extraFunctions->getTempoAt ((int64) value) : 0);
            case Vst2::audioMasterGetAutomationState:       return (pointer_sized_int) (extraFunctions != nullptr ? extraFunctions->getAutomationState() : 0);

            case Vst2::audioMasterBeginEdit:
                if (auto* param = getParameters()[index])
                    param->beginChangeGesture();
                else
                    jassertfalse; // Invalid parameter index!

                break;

            case Vst2::audioMasterEndEdit:
                if (auto* param = getParameters()[index])
                    param->endChangeGesture();
                else
                    jassertfalse; // Invalid parameter index!

                break;

            case Vst2::audioMasterPinConnected:             return isValidChannel (index, value == 0) ? 0 : 1; // (yes, 0 = true)
            case Vst2::audioMasterGetCurrentProcessLevel:   return isNonRealtime() ? 4 : 0;

            // none of these are handled (yet)...
            case Vst2::audioMasterSetTime:
            case Vst2::audioMasterGetParameterQuantization:
            case Vst2::audioMasterGetInputLatency:
            case Vst2::audioMasterGetOutputLatency:
            case Vst2::audioMasterGetPreviousPlug:
            case Vst2::audioMasterGetNextPlug:
            case Vst2::audioMasterWillReplaceOrAccumulate:
            case Vst2::audioMasterOfflineStart:
            case Vst2::audioMasterOfflineRead:
            case Vst2::audioMasterOfflineWrite:
            case Vst2::audioMasterOfflineGetCurrentPass:
            case Vst2::audioMasterOfflineGetCurrentMetaPass:
            case Vst2::audioMasterGetOutputSpeakerArrangement:
            case Vst2::audioMasterVendorSpecific:
            case Vst2::audioMasterSetIcon:
            case Vst2::audioMasterGetLanguage:
            case Vst2::audioMasterOpenWindow:
            case Vst2::audioMasterCloseWindow:
                break;

            default:
                return handleGeneralCallback (opcode, index, value, ptr, opt);
        }

        return 0;
    }

   #if JUCE_VST2_WINELIB
    /* ms_abi-attributed audioMaster thunk for Windows PE VST2 plugins.
     * Plugin's code is compiled with the Steinberg SDK where
     * audioMasterCallback has VSTCALLBACK = __cdecl (= ms_abi on
     * x86_64); plugin calls back into us through that typedef using
     * ms_abi.  A C++ lambda's call operator carries the default
     * System V calling convention and can't reliably carry
     * __attribute__((__ms_abi__)) — this static member function can.
     * We hand its address to the plugin's moduleMain via the
     * AudioMasterMS-typed slot in winelib_vst2_abi::callMain.  Body
     * is identical to the non-winelib lambda in constructEffect. */
    static __attribute__((__ms_abi__)) Vst2::VstIntPtr audioMasterCallbackThunk (Vst2::AEffect* eff,
                                                                                 Vst2::VstInt32 opcode,
                                                                                 Vst2::VstInt32 index,
                                                                                 Vst2::VstIntPtr value,
                                                                                 void* ptr,
                                                                                 float opt)
    {
        if (eff != nullptr)
            if (auto* instance = (VSTPluginInstanceHeadless*) (eff->resvd2))
                return instance->handleCallback (opcode, index, value, ptr, opt);

        return VSTPluginInstanceHeadless::handleGeneralCallback (opcode, index, value, ptr, opt);
    }
   #endif

    // handles non plugin-specific callbacks
    static pointer_sized_int handleGeneralCallback (int32 opcode, int32 /*index*/, pointer_sized_int /*value*/, void* ptr, float /*opt*/)
    {
        switch (opcode)
        {
            case Vst2::audioMasterCanDo:                        return handleCanDo ((const char*) ptr);
            case Vst2::audioMasterVersion:                      return 2400;
            case Vst2::audioMasterCurrentId:                    return shellUIDToCreate;
            case Vst2::audioMasterGetNumAutomatableParameters:  return 0;
            case Vst2::audioMasterGetAutomationState:           return 1;
            case Vst2::audioMasterGetVendorVersion:             return 0x0101;

            case Vst2::audioMasterGetVendorString:
            case Vst2::audioMasterGetProductString:             return getHostName ((char*) ptr);

            case Vst2::audioMasterGetSampleRate:                return (pointer_sized_int) defaultVSTSampleRateValue;
            case Vst2::audioMasterGetBlockSize:                 return (pointer_sized_int) defaultVSTBlockSizeValue;
            case Vst2::audioMasterSetOutputSampleRate:          return 0;

            default:
                DBG ("*** Unhandled VST Callback: " + String ((int) opcode));
                break;
        }

        return 0;
    }

    //==============================================================================
    pointer_sized_int dispatch (int opcode, int index, pointer_sized_int value, void* const ptr, float opt) const
    {
        pointer_sized_int result = 0;

        if (vstEffect != nullptr)
        {
            const ScopedLock sl (lock);
            const IdleCallRecursionPreventer icrp;

            try
            {
               #if JUCE_MAC
                auto oldResFile = CurResFile();

                if (vstModule->resFileId != 0)
                    UseResFile (vstModule->resFileId);
               #endif

               #if JUCE_VST2_WINELIB
                /* Per-instance Win32 worker: every plugin-touching
                 * dispatcher() call must run on the same Win32 thread
                 * that ran the factory / effOpen / effSetSampleRate /
                 * effSetBlockSize (see `create()`).  Plugins stash
                 * thread-affinity state in their internal
                 * CRITICAL_SECTIONs; mismatched callers trip
                 * ExitProcess assertions.  Self-dispatch guard inside
                 * `run()` lets the host callback chain (which lands
                 * back here from inside a plugin call) execute inline
                 * on the worker without re-queueing.
                 *
                 * Dispatcher is called via winelib_vst2_abi::callDispatcher
                 * which reinterpret_casts the slot to an ms_abi-typed
                 * pointer — see helpers at the top of this file. */
                if (wineDispatcher != nullptr)
                {
                    wineDispatcher->run ([&]
                    {
                        result = winelib_vst2_abi::callDispatcher (vstEffect, opcode, index, value, ptr, opt);
                    });
                }
                else
               #endif
                {
                    result = vstEffect->dispatcher (vstEffect, opcode, index, value, ptr, opt);
                }

               #if JUCE_MAC
                auto newResFile = CurResFile();

                if (newResFile != oldResFile)  // avoid confusing the parent app's resource file with the plug-in's
                {
                    vstModule->resFileId = newResFile;
                    UseResFile (oldResFile);
                }
               #endif
            }
            catch (...)
            {}
        }

        return result;
    }

    bool loadFromFXBFile (const void* const data, const size_t dataSize)
    {
        if (dataSize < 28)
            return false;

        auto set = (const fxSet*) data;

        if ((! compareMagic (set->chunkMagic, "CcnK")) || fxbSwap (set->version) > fxbVersionNum)
            return false;

        if (compareMagic (set->fxMagic, "FxBk"))
        {
            // bank of programs
            if (fxbSwap (set->numPrograms) >= 0)
            {
                auto oldProg = getCurrentProgram();
                auto numParams = fxbSwap (((const fxProgram*) (set->programs))->numParams);
                auto progLen = (int) sizeof (fxProgram) + (numParams - 1) * (int) sizeof (float);

                for (int i = 0; i < fxbSwap (set->numPrograms); ++i)
                {
                    if (i != oldProg)
                    {
                        auto prog = addBytesToPointer (set->programs, i * progLen);

                        if (getAddressDifference (prog, set) >= (int) dataSize)
                            return false;

                        if (fxbSwap (set->numPrograms) > 0)
                            setCurrentProgram (i);

                        if (! restoreProgramSettings (prog))
                            return false;
                    }
                }

                if (fxbSwap (set->numPrograms) > 0)
                    setCurrentProgram (oldProg);

                auto prog = addBytesToPointer (set->programs, oldProg * progLen);

                if (getAddressDifference (prog, set) >= (int) dataSize)
                    return false;

                if (! restoreProgramSettings (prog))
                    return false;
            }
        }
        else if (compareMagic (set->fxMagic, "FxCk"))
        {
            // single program
            auto prog = (const fxProgram*) data;

            if (! compareMagic (prog->chunkMagic, "CcnK"))
                return false;

            changeProgramName (getCurrentProgram(), prog->prgName);

            for (int i = 0; i < fxbSwap (prog->numParams); ++i)
                if (auto* param = getParameters()[i])
                    param->setValue (fxbSwapFloat (prog->params[i]));
        }
        else if (compareMagic (set->fxMagic, "FBCh"))
        {
            // non-preset chunk
            auto cset = (const fxChunkSet*) data;

            if ((size_t) fxbSwap (cset->chunkSize) + sizeof (fxChunkSet) - 8 > (size_t) dataSize)
                return false;

            setChunkData (cset->chunk, fxbSwap (cset->chunkSize), false);
        }
        else if (compareMagic (set->fxMagic, "FPCh"))
        {
            // preset chunk
            auto cset = (const fxProgramSet*) data;

            if ((size_t) fxbSwap (cset->chunkSize) + sizeof (fxProgramSet) - 8 > (size_t) dataSize)
                return false;

            setChunkData (cset->chunk, fxbSwap (cset->chunkSize), true);

            changeProgramName (getCurrentProgram(), cset->name);
        }
        else
        {
            return false;
        }

        return true;
    }

    bool saveToFXBFile (MemoryBlock& dest, bool isFXB, int maxSizeMB = 128)
    {
        auto numPrograms = getNumPrograms();
        auto numParams = getParameters().size();

        if (usesChunks())
        {
            MemoryBlock chunk;
            getChunkData (chunk, ! isFXB, maxSizeMB);

            if (isFXB)
            {
                auto totalLen = sizeof (fxChunkSet) + chunk.getSize() - 8;
                dest.setSize (totalLen, true);

                auto set = (fxChunkSet*) dest.getData();
                set->chunkMagic = fxbName ("CcnK");
                set->byteSize = 0;
                set->fxMagic = fxbName ("FBCh");
                set->version = fxbSwap (fxbVersionNum);
                set->fxID = fxbSwap (getUID());
                set->fxVersion = fxbSwap (getVersionNumber());
                set->numPrograms = fxbSwap (numPrograms);
                set->chunkSize = fxbSwap ((int32) chunk.getSize());

                chunk.copyTo (set->chunk, 0, chunk.getSize());
            }
            else
            {
                auto totalLen = sizeof (fxProgramSet) + chunk.getSize() - 8;
                dest.setSize (totalLen, true);

                auto set = (fxProgramSet*) dest.getData();
                set->chunkMagic = fxbName ("CcnK");
                set->byteSize = 0;
                set->fxMagic = fxbName ("FPCh");
                set->version = fxbSwap (fxbVersionNum);
                set->fxID = fxbSwap (getUID());
                set->fxVersion = fxbSwap (getVersionNumber());
                set->numPrograms = fxbSwap (numPrograms);
                set->chunkSize = fxbSwap ((int32) chunk.getSize());

                getCurrentProgramName().copyToUTF8 (set->name, sizeof (set->name) - 1);
                chunk.copyTo (set->chunk, 0, chunk.getSize());
            }
        }
        else
        {
            if (isFXB)
            {
                auto progLen = (int) sizeof (fxProgram) + (numParams - 1) * (int) sizeof (float);
                auto len = (size_t) (progLen * jmax (1, numPrograms)) + (sizeof (fxSet) - sizeof (fxProgram));
                dest.setSize (len, true);

                auto set = (fxSet*) dest.getData();
                set->chunkMagic = fxbName ("CcnK");
                set->byteSize = 0;
                set->fxMagic = fxbName ("FxBk");
                set->version = fxbSwap (fxbVersionNum);
                set->fxID = fxbSwap (getUID());
                set->fxVersion = fxbSwap (getVersionNumber());
                set->numPrograms = fxbSwap (numPrograms);

                MemoryBlock oldSettings;
                createTempParameterStore (oldSettings);

                auto oldProgram = getCurrentProgram();

                if (oldProgram >= 0)
                    setParamsInProgramBlock (addBytesToPointer (set->programs, oldProgram * progLen));

                for (int i = 0; i < numPrograms; ++i)
                {
                    if (i != oldProgram)
                    {
                        setCurrentProgram (i);
                        setParamsInProgramBlock (addBytesToPointer (set->programs, i * progLen));
                    }
                }

                if (oldProgram >= 0)
                    setCurrentProgram (oldProgram);

                restoreFromTempParameterStore (oldSettings);
            }
            else
            {
                dest.setSize ((size_t) ((numParams - 1) * (int) sizeof (float)) + sizeof (fxProgram), true);
                setParamsInProgramBlock ((fxProgram*) dest.getData());
            }
        }

        return true;
    }

    bool usesChunks() const noexcept        { return vstEffect != nullptr && (vstEffect->flags & Vst2::effFlagsProgramChunks) != 0; }

    bool getChunkData (MemoryBlock& mb, bool isPreset, int maxSizeMB) const
    {
        if (usesChunks())
        {
            void* data = nullptr;
            auto bytes = (size_t) dispatch (Vst2::effGetChunk, isPreset ? 1 : 0, 0, &data, 0.0f);

            if (data != nullptr && bytes <= (size_t) maxSizeMB * 1024 * 1024)
            {
                mb.setSize (bytes);
                mb.copyFrom (data, 0, bytes);

                return true;
            }
        }

        return false;
    }

    bool setChunkData (const void* data, const int size, bool isPreset)
    {
        if (size > 0 && usesChunks())
        {
            dispatch (Vst2::effSetChunk, isPreset ? 1 : 0, size, (void*) data, 0.0f);

            if (! isPreset)
                updateStoredProgramNames();

            return true;
        }

        return false;
    }

    virtual bool updateSizeFromEditor (int, int) { return false; }

    Vst2::AEffect* vstEffect;
    ModuleHandle::Ptr vstModule;

    std::unique_ptr<VSTPluginFormatHeadless::ExtraFunctions> extraFunctions;

private:
    //==============================================================================
    struct VST2BypassParameter final : public Parameter
    {
        VST2BypassParameter (VSTPluginInstanceHeadless& effectToUse)
            : parent (effectToUse),
              vstOnStrings (TRANS ("on"), TRANS ("yes"), TRANS ("true")),
              vstOffStrings (TRANS ("off"), TRANS ("no"), TRANS ("false")),
              values (TRANS ("Off"), TRANS ("On"))
        {
        }

        void setValue (float newValue) override
        {
            currentValue = (! approximatelyEqual (newValue, 0.0f));

            if (parent.vstSupportsBypass)
                parent.dispatch (Vst2::effSetBypass, 0, currentValue ? 1 : 0, nullptr, 0.0f);
        }

        float getValueForText (const String& text) const override
        {
            String lowercaseText (text.toLowerCase());

            for (auto& testText : vstOnStrings)
                if (lowercaseText == testText)
                    return 1.0f;

            for (auto& testText : vstOffStrings)
                if (lowercaseText == testText)
                    return 0.0f;

            return text.getIntValue() != 0 ? 1.0f : 0.0f;
        }

        float getValue() const override                                     { return currentValue; }
        float getDefaultValue() const override                              { return 0.0f; }
        String getName (int /*maximumStringLength*/) const override         { return "Bypass"; }
        String getText (float value, int) const override                    { return (! approximatelyEqual (value, 0.0f) ? TRANS ("On") : TRANS ("Off")); }
        bool isAutomatable() const override                                 { return true; }
        bool isDiscrete() const override                                    { return true; }
        bool isBoolean() const override                                     { return true; }
        int getNumSteps() const override                                    { return 2; }
        StringArray getAllValueStrings() const override                     { return values; }
        String getLabel() const override                                    { return {}; }
        String getParameterID() const override                              { return {}; }

        VSTPluginInstanceHeadless& parent;
        bool currentValue = false;
        StringArray vstOnStrings, vstOffStrings, values;
    };

    //==============================================================================
    String name;
    CriticalSection lock;
    std::atomic<bool> wantsMidiMessages { false };
    bool initialised = false;
    std::atomic<bool> isPowerOn { false };
    bool lastProcessBlockCallWasBypass = false, vstSupportsBypass = false;
    mutable StringArray programNames;
    AudioBuffer<float> outOfPlaceBuffer;
    TempChannelPointers tempChannelPointers[2];

    CriticalSection midiInLock;
    MidiBuffer incomingMidi;
    VSTMidiEventList midiEventsToSend;
    Vst2::VstTimeInfo vstHostTime;

    AudioBuffer<float> tmpBufferFloat;
    HeapBlock<float*> channelBufferFloat;

    AudioBuffer<double> tmpBufferDouble;
    HeapBlock<double*> channelBufferDouble;
    std::unique_ptr<VST2BypassParameter> bypassParam;

    std::unique_ptr<VSTXMLInfo> xmlInfo;

    static pointer_sized_int handleCanDo (const char* name)
    {
        static const char* canDos[] = { "supplyIdle",
                                        "sendVstEvents",
                                        "sendVstMidiEvent",
                                        "sendVstTimeInfo",
                                        "receiveVstEvents",
                                        "receiveVstMidiEvent",
                                        "supportShell",
                                        "sizeWindow",
                                        "shellCategory" };

        for (int i = 0; i < numElementsInArray (canDos); ++i)
            if (strcmp (canDos[i], name) == 0)
                return 1;

        return 0;
    }

    static pointer_sized_int getHostName (char* name)
    {
        String hostName (JUCE_VST_FALLBACK_HOST_NAME);

        if (auto* app = JUCEApplicationBase::getInstance())
            hostName = app->getApplicationName();

        hostName.copyToUTF8 (name, (size_t) jmin (Vst2::kVstMaxVendorStrLen, Vst2::kVstMaxProductStrLen) - 1);
        return 1;
    }

    pointer_sized_int getVSTTime() noexcept
    {
        JUCE_BEGIN_IGNORE_WARNINGS_MSVC (4311)

        return (pointer_sized_int) &vstHostTime;

        JUCE_END_IGNORE_WARNINGS_MSVC
    }

    virtual void updateDisplay() {}
    virtual void handleIdle() {}
    virtual void needIdle() {}

    void setWindowSize (int width, int height)
    {
       #if JUCE_LINUX || JUCE_BSD
        const MessageManagerLock mmLock;
       #endif

        updateSizeFromEditor (width, height);
    }

    //==============================================================================
    static Vst2::AEffect* constructEffect (const ModuleHandle::Ptr& module)
    {
        Vst2::AEffect* effect = nullptr;
        try
        {
            const IdleCallRecursionPreventer icrp;
            _fpreset();

            JUCE_VST_LOG ("Creating VST instance: " + module->pluginName);

           #if JUCE_MAC
            if (module->resFileId != 0)
                UseResFile (module->resFileId);
           #endif

           #if JUCE_VST2_WINELIB
            /* Use the ms_abi-attributed static thunk and call the
             * plugin's moduleMain via the ms_abi-typed wrapper —
             * matches the calling convention the PE DLL was compiled
             * with.  See winelib_vst2_abi (top of file) for the
             * background. */
            winelib_vst2_abi::AudioMasterMS audioMaster = &VSTPluginInstanceHeadless::audioMasterCallbackThunk;
            effect = winelib_vst2_abi::callMain (module->moduleMain, audioMaster);
           #else
            constexpr Vst2::audioMasterCallback audioMaster = [] (Vst2::AEffect* eff,
                                                                  Vst2::VstInt32 opcode,
                                                                  Vst2::VstInt32 index,
                                                                  Vst2::VstIntPtr value,
                                                                  void* ptr,
                                                                  float opt) -> Vst2::VstIntPtr
            {
                if (eff != nullptr)
                    if (auto* instance = (VSTPluginInstanceHeadless*) (eff->resvd2))
                        return instance->handleCallback (opcode, index, value, ptr, opt);

                return VSTPluginInstanceHeadless::handleGeneralCallback (opcode, index, value, ptr, opt);
            };

            {
                JUCE_VST_WRAPPER_INVOKE_MAIN
            }
           #endif

            if (effect != nullptr && effect->magic == 0x56737450 /* 'VstP' */)
            {
                jassert (effect->resvd2 == 0);
                jassert (effect->object != nullptr);

                _fpreset(); // some dodgy plugs mess around with this
            }
            else
            {
                effect = nullptr;
            }
        }
        catch (...)
        {}

        return effect;
    }

    static BusesProperties queryBusIO (Vst2::AEffect* effect)
    {
        BusesProperties returnValue;

        if (effect->numInputs == 0 && effect->numOutputs == 0)
            return returnValue;

        // Workaround for old broken JUCE plug-ins which would return an invalid
        // speaker arrangement if the host didn't ask for a specific arrangement
        // beforehand.
        // Check if the plug-in reports any default layouts. If it doesn't, then
        // try setting a default layout compatible with the number of pins this
        // plug-in is reporting.
        if (! pluginHasDefaultChannelLayouts (effect))
        {
            SpeakerMappings::VstSpeakerConfigurationHolder canonicalIn  (AudioChannelSet::canonicalChannelSet (effect->numInputs));
            SpeakerMappings::VstSpeakerConfigurationHolder canonicalOut (AudioChannelSet::canonicalChannelSet (effect->numOutputs));

           #if JUCE_VST2_WINELIB
            winelib_vst2_abi::callDispatcher (effect, Vst2::effSetSpeakerArrangement, 0,
                                              (pointer_sized_int) &canonicalIn.get(), (void*) &canonicalOut.get(), 0.0f);
           #else
            effect->dispatcher (effect, Vst2::effSetSpeakerArrangement, 0,
                                      (pointer_sized_int) &canonicalIn.get(), (void*) &canonicalOut.get(), 0.0f);
           #endif
        }

        const auto arrangement = getSpeakerArrangementWrapper (effect);

        for (int dir = 0; dir < 2; ++dir)
        {
            const bool isInput = (dir == 0);
            const int opcode = (isInput ? Vst2::effGetInputProperties : Vst2::effGetOutputProperties);
            const int maxChannels = (isInput ? effect->numInputs : effect->numOutputs);
            const auto* arr = (isInput ? arrangement.in : arrangement.out);
            bool busAdded = false;

            Vst2::VstPinProperties pinProps;
            AudioChannelSet layout;

            for (int ch = 0; ch < maxChannels; ch += layout.size())
            {
               #if JUCE_VST2_WINELIB
                if (winelib_vst2_abi::callDispatcher (effect, opcode, ch, 0, &pinProps, 0.0f) == 0)
                    break;
               #else
                if (effect->dispatcher (effect, opcode, ch, 0, &pinProps, 0.0f) == 0)
                    break;
               #endif

                if ((pinProps.flags & Vst2::kVstPinUseSpeaker) != 0)
                {
                    layout = SpeakerMappings::vstArrangementTypeToChannelSet (pinProps.arrangementType, 0);

                    if (layout.isDisabled())
                        break;
                }
                else if (arr == nullptr)
                {
                    layout = ((pinProps.flags & Vst2::kVstPinIsStereo) != 0 ? AudioChannelSet::stereo() : AudioChannelSet::mono());
                }
                else
                    break;

                busAdded = true;
                returnValue.addBus (isInput, pinProps.label, layout, true);
            }

            // no buses?
            if (! busAdded && maxChannels > 0)
            {
                String busName = (isInput ? "Input" : "Output");

               #if JUCE_VST2_WINELIB
                if (winelib_vst2_abi::callDispatcher (effect, opcode, 0, 0, &pinProps, 0.0f) != 0)
                    busName = pinProps.label;
               #else
                if (effect->dispatcher (effect, opcode, 0, 0, &pinProps, 0.0f) != 0)
                    busName = pinProps.label;
               #endif

                if (arr != nullptr)
                    layout = SpeakerMappings::vstArrangementTypeToChannelSet (*arr);
                else
                    layout = AudioChannelSet::canonicalChannelSet (maxChannels);

                returnValue.addBus (isInput, busName, layout, true);
            }
        }

        return returnValue;
    }

    static bool pluginHasDefaultChannelLayouts (Vst2::AEffect* effect)
    {
        if (getSpeakerArrangementWrapper (effect).isValid())
            return true;

        for (int dir = 0; dir < 2; ++dir)
        {
            const bool isInput = (dir == 0);
            const int opcode = (isInput ? Vst2::effGetInputProperties : Vst2::effGetOutputProperties);
            const int maxChannels = (isInput ? effect->numInputs : effect->numOutputs);

            int channels = 1;

            for (int ch = 0; ch < maxChannels; ch += channels)
            {
                Vst2::VstPinProperties pinProps;

               #if JUCE_VST2_WINELIB
                if (winelib_vst2_abi::callDispatcher (effect, opcode, ch, 0, &pinProps, 0.0f) == 0)
                    return false;
               #else
                if (effect->dispatcher (effect, opcode, ch, 0, &pinProps, 0.0f) == 0)
                    return false;
               #endif

                if ((pinProps.flags & Vst2::kVstPinUseSpeaker) != 0)
                    return true;

                channels = (pinProps.flags & Vst2::kVstPinIsStereo) != 0 ? 2 : 1;
            }
        }

        return false;
    }

    struct SpeakerArrangements
    {
        const Vst2::VstSpeakerArrangement* in;
        const Vst2::VstSpeakerArrangement* out;

        bool isValid() const noexcept { return in != nullptr && out != nullptr; }
    };

    static SpeakerArrangements getSpeakerArrangementWrapper (Vst2::AEffect* effect)
    {
        // Workaround: unfortunately old JUCE VST-2 plug-ins had a bug and would crash if
        // you try to get the speaker arrangement when there are no input channels present.
        // Hopefully, one day (when there are no more old JUCE plug-ins around), we can
        // comment out the next two lines.
        if (effect->numInputs == 0)
            return { nullptr, nullptr };

        SpeakerArrangements result { nullptr, nullptr };
       #if JUCE_VST2_WINELIB
        const auto dispatchResult = winelib_vst2_abi::callDispatcher (effect,
                                                                       Vst2::effGetSpeakerArrangement,
                                                                       0,
                                                                       reinterpret_cast<pointer_sized_int> (&result.in),
                                                                       &result.out,
                                                                       0.0f);
       #else
        const auto dispatchResult = effect->dispatcher (effect,
                                                        Vst2::effGetSpeakerArrangement,
                                                        0,
                                                        reinterpret_cast<pointer_sized_int> (&result.in),
                                                        &result.out,
                                                        0.0f);
       #endif

        if (dispatchResult != 0)
            return result;

        return { nullptr, nullptr };
    }

    template <typename Member, typename Value>
    void setFromOptional (Member& target, Optional<Value> opt, int32_t flag)
    {
        if (opt.hasValue())
        {
            target = static_cast<Member> (*opt);
            vstHostTime.flags |= flag;
        }
        else
        {
            vstHostTime.flags &= ~flag;
        }
    }

    //==============================================================================
    template <typename FloatType>
    void processAudio (AudioBuffer<FloatType>& buffer, MidiBuffer& midiMessages,
                       AudioBuffer<FloatType>& tmpBuffer,
                       HeapBlock<FloatType*>& channelBuffer,
                       bool processBlockBypassedCalled)
    {
        if (vstSupportsBypass)
        {
            updateBypass (processBlockBypassedCalled);
        }
        else if (processBlockBypassedCalled)
        {
            // if this vst does not support bypass then we will have to do this ourselves
            AudioProcessor::processBlockBypassed (buffer, midiMessages);
            return;
        }

        auto numSamples  = buffer.getNumSamples();
        auto numChannels = buffer.getNumChannels();

        if (initialised)
        {
            if (auto* currentPlayHead = getPlayHead())
            {
                if (const auto position = currentPlayHead->getPosition())
                {
                    if (const auto samplePos = position->getTimeInSamples())
                        vstHostTime.samplePos = (double) *samplePos;
                    else
                        jassertfalse; // VST hosts *must* call setTimeInSamples on the audio playhead

                    if (auto sig = position->getTimeSignature())
                    {
                        vstHostTime.flags |= Vst2::kVstTimeSigValid;
                        vstHostTime.timeSigNumerator   = sig->numerator;
                        vstHostTime.timeSigDenominator = sig->denominator;
                    }
                    else
                    {
                        vstHostTime.flags &= ~Vst2::kVstTimeSigValid;
                    }

                    setFromOptional (vstHostTime.ppqPos,      position->getPpqPosition(),               Vst2::kVstPpqPosValid);
                    setFromOptional (vstHostTime.barStartPos, position->getPpqPositionOfLastBarStart(), Vst2::kVstBarsValid);
                    setFromOptional (vstHostTime.nanoSeconds, position->getHostTimeNs(),                Vst2::kVstNanosValid);
                    setFromOptional (vstHostTime.tempo,       position->getBpm(),                       Vst2::kVstTempoValid);

                    int32 newTransportFlags = 0;
                    if (position->getIsPlaying())     newTransportFlags |= Vst2::kVstTransportPlaying;
                    if (position->getIsRecording())   newTransportFlags |= Vst2::kVstTransportRecording;

                    if (newTransportFlags != (vstHostTime.flags & (Vst2::kVstTransportPlaying
                                                                   | Vst2::kVstTransportRecording)))
                        vstHostTime.flags = (vstHostTime.flags & ~(Vst2::kVstTransportPlaying | Vst2::kVstTransportRecording)) | newTransportFlags | Vst2::kVstTransportChanged;
                    else
                        vstHostTime.flags &= ~Vst2::kVstTransportChanged;

                    const auto optionalFrameRate = [fr = position->getFrameRate()]() -> Optional<Vst2::VstInt32>
                    {
                        if (! fr.hasValue())
                            return {};

                        switch (fr->getBaseRate())
                        {
                            case 24:        return fr->isPullDown() ? Vst2::kVstSmpte239fps : Vst2::kVstSmpte24fps;
                            case 25:        return fr->isPullDown() ? Vst2::kVstSmpte249fps : Vst2::kVstSmpte25fps;
                            case 30:        return fr->isPullDown() ? (fr->isDrop() ? Vst2::kVstSmpte2997dfps : Vst2::kVstSmpte2997fps)
                                                                    : (fr->isDrop() ? Vst2::kVstSmpte30dfps   : Vst2::kVstSmpte30fps);
                            case 60:        return fr->isPullDown() ? Vst2::kVstSmpte599fps : Vst2::kVstSmpte60fps;
                        }

                        return {};
                    }();

                    vstHostTime.flags |= optionalFrameRate ? Vst2::kVstSmpteValid : 0;
                    vstHostTime.smpteFrameRate = optionalFrameRate.orFallback (Vst2::VstSmpteFrameRate{});
                    const auto effectiveRate = position->getFrameRate().hasValue() ? position->getFrameRate()->getEffectiveRate() : 0.0;
                    vstHostTime.smpteOffset = (int32) (position->getTimeInSeconds().orFallback (0.0) * 80.0 * effectiveRate + 0.5);

                    if (const auto loop = position->getLoopPoints())
                    {
                        vstHostTime.flags |= Vst2::kVstCyclePosValid;
                        vstHostTime.cycleStartPos = loop->ppqStart;
                        vstHostTime.cycleEndPos   = loop->ppqEnd;
                    }
                    else
                    {
                        vstHostTime.flags &= ~Vst2::kVstCyclePosValid;
                    }

                    if (position->getIsLooping())
                        vstHostTime.flags |= Vst2::kVstTransportCycleActive;
                    else
                        vstHostTime.flags &= ~Vst2::kVstTransportCycleActive;
                }
            }

            vstHostTime.nanoSeconds = getVSTHostTimeNanoseconds();

            if (wantsMidiMessages)
            {
                midiEventsToSend.clear();
                midiEventsToSend.ensureSize (1);

                for (const auto metadata : midiMessages)
                    midiEventsToSend.addEvent (metadata.data, metadata.numBytes,
                                               jlimit (0, numSamples - 1, metadata.samplePosition));

               #if JUCE_VST2_WINELIB
                /* effProcessEvents is a plugin call invoked on the
                 * audio thread just before processReplacing.  Same
                 * thread-identity + ABI rules apply — route through
                 * the per-instance worker AND through the ms_abi
                 * callDispatcher helper. */
                if (wineDispatcher != nullptr)
                {
                    wineDispatcher->run ([&]
                    {
                        try
                        {
                            winelib_vst2_abi::callDispatcher (vstEffect, Vst2::effProcessEvents, 0, 0, midiEventsToSend.events, 0);
                        }
                        catch (...) {}
                    });
                }
                else
               #endif
                {
                    vstEffect->dispatcher (vstEffect, Vst2::effProcessEvents, 0, 0, midiEventsToSend.events, 0);
                }
            }

            _clearfp();

            // always ensure that the buffer is at least as large as the maximum number of channels
            auto maxChannels = jmax (vstEffect->numInputs, vstEffect->numOutputs);
            auto channels = channelBuffer.get();

            if (numChannels < maxChannels)
            {
                if (numSamples > tmpBuffer.getNumSamples())
                    tmpBuffer.setSize (tmpBuffer.getNumChannels(), numSamples);

                tmpBuffer.clear();
            }

            for (int ch = 0; ch < maxChannels; ++ch)
                channels[ch] = (ch < numChannels ? buffer.getWritePointer (ch) : tmpBuffer.getWritePointer (ch));

            {
                AudioBuffer<FloatType> processBuffer (channels, maxChannels, numSamples);

                invokeProcessFunction (processBuffer, numSamples);
            }
        }
        else
        {
            // Not initialised, so just bypass.
            for (int i = getTotalNumOutputChannels(); --i >= 0;)
                buffer.clear (i, 0, buffer.getNumSamples());
        }

        {
            // Copy any incoming midi.
            const ScopedLock sl (midiInLock);

            midiMessages.swapWith (incomingMidi);
            incomingMidi.clear();
        }
    }

    //==============================================================================
    inline void invokeProcessFunction (AudioBuffer<float>& buffer, int32 sampleFrames)
    {
       #if JUCE_VST2_WINELIB
        /* Audio hot path — dispatch to the per-instance Win32 worker so
         * the plugin's process() runs with the same Win32 thread
         * identity it had during effOpen / effSetSampleRate /
         * effSetBlockSize.  Per-instance worker, so N VST2 instances
         * across N JACK-callback dispatches run on N parallel workers
         * (no global bottleneck).  Two FUTEX_*_PI hops on a PiMutex +
         * PiCond pair from juce_winelib_pi_sync — the JACK thread's PI
         * chain carries through the worker for the duration of
         * process(), no inversion gap exists between dispatch and
         * wake.  Same shape as VST3's processor->process dispatch in
         * juce_VST3PluginFormatImpl.h. */
        if (wineDispatcher != nullptr)
        {
            wineDispatcher->run ([&]
            {
                try
                {
                    if ((vstEffect->flags & Vst2::effFlagsCanReplacing) != 0)
                    {
                        winelib_vst2_abi::callProcessReplacing (vstEffect,
                                                                tempChannelPointers[0].getArrayOfModifiableWritePointers (buffer),
                                                                tempChannelPointers[1].getArrayOfModifiableWritePointers (buffer),
                                                                sampleFrames);
                    }
                    else
                    {
                        outOfPlaceBuffer.setSize (vstEffect->numOutputs, sampleFrames);
                        outOfPlaceBuffer.clear();

                        winelib_vst2_abi::callProcessLegacy (vstEffect,
                                                             tempChannelPointers[0].getArrayOfModifiableWritePointers (buffer),
                                                             tempChannelPointers[1].getArrayOfModifiableWritePointers (outOfPlaceBuffer),
                                                             sampleFrames);

                        for (int i = vstEffect->numOutputs; --i >= 0;)
                            buffer.copyFrom (i, 0, outOfPlaceBuffer.getReadPointer (i), sampleFrames);
                    }
                }
                catch (...)
                {
                    // Plugin process() threw — quietly drop the block
                    // rather than propagating the exception into
                    // Element's audio engine (would terminate the
                    // process under SCHED_FIFO@80).  Same shape as
                    // dispatch()'s try/catch.
                }
            });
            return;
        }
       #endif

        if ((vstEffect->flags & Vst2::effFlagsCanReplacing) != 0)
        {
            vstEffect->processReplacing (vstEffect, tempChannelPointers[0].getArrayOfModifiableWritePointers (buffer),
                                                    tempChannelPointers[1].getArrayOfModifiableWritePointers (buffer), sampleFrames);
        }
        else
        {
            outOfPlaceBuffer.setSize (vstEffect->numOutputs, sampleFrames);
            outOfPlaceBuffer.clear();

            vstEffect->process (vstEffect, tempChannelPointers[0].getArrayOfModifiableWritePointers (buffer),
                                           tempChannelPointers[1].getArrayOfModifiableWritePointers (outOfPlaceBuffer), sampleFrames);

            for (int i = vstEffect->numOutputs; --i >= 0;)
                buffer.copyFrom (i, 0, outOfPlaceBuffer.getReadPointer (i), sampleFrames);
        }
    }

    inline void invokeProcessFunction (AudioBuffer<double>& buffer, int32 sampleFrames)
    {
       #if JUCE_VST2_WINELIB
        if (wineDispatcher != nullptr)
        {
            wineDispatcher->run ([&]
            {
                try
                {
                    winelib_vst2_abi::callProcessDoubleReplacing (vstEffect,
                                                                  tempChannelPointers[0].getArrayOfModifiableWritePointers (buffer),
                                                                  tempChannelPointers[1].getArrayOfModifiableWritePointers (buffer),
                                                                  sampleFrames);
                }
                catch (...) {}
            });
            return;
        }
       #endif
        vstEffect->processDoubleReplacing (vstEffect, tempChannelPointers[0].getArrayOfModifiableWritePointers (buffer),
                                                      tempChannelPointers[1].getArrayOfModifiableWritePointers (buffer), sampleFrames);
    }

    //==============================================================================
    bool restoreProgramSettings (const fxProgram* const prog)
    {
        if (compareMagic (prog->chunkMagic, "CcnK")
             && compareMagic (prog->fxMagic, "FxCk"))
        {
            changeProgramName (getCurrentProgram(), prog->prgName);

            for (int i = 0; i < fxbSwap (prog->numParams); ++i)
                if (auto* param = getParameters()[i])
                    param->setValue (fxbSwapFloat (prog->params[i]));

            return true;
        }

        return false;
    }

    String getTextForOpcode (const int index, const int opcode) const
    {
        if (vstEffect == nullptr)
            return {};

        jassert (index >= 0 && index < vstEffect->numParams);
        char nm[256] = { 0 };
        dispatch (opcode, index, 0, nm, 0);
        return String::createStringFromData (nm, (int) sizeof (nm)).trim();
    }

    String getCurrentProgramName()
    {
        String progName;

        if (vstEffect != nullptr)
        {
            {
                char nm[256] = { 0 };
                dispatch (Vst2::effGetProgramName, 0, 0, nm, 0);
                progName = String::createStringFromData (nm, (int) sizeof (nm)).trim();
            }

            const int index = getCurrentProgram();

            if (index >= 0 && programNames[index].isEmpty())
            {
                while (programNames.size() < index)
                    programNames.add (String());

                programNames.set (index, progName);
            }
        }

        return progName;
    }

    void setParamsInProgramBlock (fxProgram* prog)
    {
        auto numParams = getParameters().size();

        prog->chunkMagic = fxbName ("CcnK");
        prog->byteSize = 0;
        prog->fxMagic = fxbName ("FxCk");
        prog->version = fxbSwap (fxbVersionNum);
        prog->fxID = fxbSwap (getUID());
        prog->fxVersion = fxbSwap (getVersionNumber());
        prog->numParams = fxbSwap (numParams);

        getCurrentProgramName().copyToUTF8 (prog->prgName, sizeof (prog->prgName) - 1);

        for (int i = 0; i < numParams; ++i)
            if (auto* param = getParameters()[i])
                prog->params[i] = fxbSwapFloat (param->getValue());
    }

    void updateStoredProgramNames()
    {
        if (vstEffect != nullptr && getNumPrograms() > 0)
        {
            char nm[256] = { 0 };

            // only do this if the plugin can't use indexed names
            if (dispatch (Vst2::effGetProgramNameIndexed, 0, -1, nm, 0) == 0)
            {
                auto oldProgram = getCurrentProgram();
                MemoryBlock oldSettings;
                createTempParameterStore (oldSettings);

                for (int i = 0; i < getNumPrograms(); ++i)
                {
                    setCurrentProgram (i);
                    getCurrentProgramName();  // (this updates the list)
                }

                setCurrentProgram (oldProgram);
                restoreFromTempParameterStore (oldSettings);
            }
        }
    }

    void handleMidiFromPlugin (const Vst2::VstEvents* events)
    {
        if (events != nullptr)
        {
            const ScopedLock sl (midiInLock);
            VSTMidiEventList::addEventsToMidiBuffer (events, incomingMidi);
        }
    }

    //==============================================================================
    void createTempParameterStore (MemoryBlock& dest)
    {
        auto numParameters = getParameters().size();
        dest.setSize (64 + 4 * (size_t) numParameters);
        dest.fillWith (0);

        getCurrentProgramName().copyToUTF8 ((char*) dest.getData(), 63);

        auto p = unalignedPointerCast<float*> (((char*) dest.getData()) + 64);

        for (int i = 0; i < numParameters; ++i)
            if (auto* param = getParameters()[i])
                p[i] = param->getValue();
    }

    void restoreFromTempParameterStore (const MemoryBlock& m)
    {
        changeProgramName (getCurrentProgram(), (const char*) m.getData());

        auto p = unalignedPointerCast<float*> (((char*) m.getData()) + 64);
        auto numParameters = getParameters().size();

        for (int i = 0; i < numParameters; ++i)
            if (auto* param = getParameters()[i])
                param->setValue (p[i]);
    }

    pointer_sized_int getVstDirectory() const
    {
       #if JUCE_MAC
        return (pointer_sized_int) (void*) &vstModule->parentDirFSSpec;
       #else
        return (pointer_sized_int) (pointer_sized_uint) vstModule->fullParentDirectoryPathName.toRawUTF8();
       #endif
    }

    //==============================================================================
    int getVersionNumber() const noexcept   { return vstEffect != nullptr ? vstEffect->version : 0; }

    String getVersion() const
    {
        auto v = (unsigned int) dispatch (Vst2::effGetVendorVersion, 0, 0, nullptr, 0);

        String s;

        if (v == 0 || (int) v == -1)
            v = (unsigned int) getVersionNumber();

        if (v != 0)
        {
            // See yfede's post for the rational on this encoding
            // https://forum.juce.com/t/issues-with-version-integer-reported-by-vst2/23867/6

            unsigned int major = 0, minor = 0, bugfix = 0, build = 0;

            if (v < 10)            // Encoding A
            {
                major = v;
            }
            else if (v < 10000)    // Encoding B
            {
                major  = (v / 1000);
                minor  = (v % 1000) / 100;
                bugfix = (v % 100)  / 10;
                build  = (v % 10);
            }
            else if (v < 0x10000)  // Encoding C
            {
                major  = (v / 10000);
                minor  = (v % 10000) / 1000;
                bugfix = (v % 1000)  / 100;
                build  = (v % 100)   / 10;
            }
            else if (v < 0x650000) // Encoding D
            {
                major  = (v >> 16) & 0xff;
                minor  = (v >> 8)  & 0xff;
                bugfix = (v >> 0)  & 0xff;
            }
            else                  // Encoding E
            {
                major  = (v / 10000000);
                minor  = (v % 10000000) / 100000;
                bugfix = (v % 100000)   / 1000;
                build  = (v % 1000);
            }

            s << (int) major << '.' << (int) minor << '.' << (int) bugfix << '.' << (int) build;
        }

        return s;
    }

    const char* getCategory() const
    {
        switch (getVstCategory())
        {
            case Vst2::kPlugCategEffect:          return "Effect";
            case Vst2::kPlugCategSynth:           return "Synth";
            case Vst2::kPlugCategAnalysis:        return "Analysis";
            case Vst2::kPlugCategMastering:       return "Mastering";
            case Vst2::kPlugCategSpacializer:     return "Spacial";
            case Vst2::kPlugCategRoomFx:          return "Reverb";
            case Vst2::kPlugSurroundFx:           return "Surround";
            case Vst2::kPlugCategRestoration:     return "Restoration";
            case Vst2::kPlugCategGenerator:       return "Tone generation";
            case Vst2::kPlugCategOfflineProcess:  return "Offline Process";
            case Vst2::kPlugCategShell:           return "Shell";
            case Vst2::kPlugCategUnknown:         return "Unknown";
            case Vst2::kPlugCategMaxCount:
            default:                              break;
        }

        return nullptr;
    }

    void setPower (const bool on)
    {
        dispatch (Vst2::effMainsChanged, 0, on ? 1 : 0, nullptr, 0);
        isPowerOn = on;
    }

    //==============================================================================
    void updateBypass (bool processBlockBypassedCalled)
    {
        if (processBlockBypassedCalled)
        {
            if (approximatelyEqual (bypassParam->getValue(), 0.0f) || ! lastProcessBlockCallWasBypass)
                bypassParam->setValue (1.0f);
        }
        else
        {
            if (lastProcessBlockCallWasBypass)
                bypassParam->setValue (0.0f);
        }

        lastProcessBlockCallWasBypass = processBlockBypassedCalled;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VSTPluginInstanceHeadless)
};

//==============================================================================
//==============================================================================
// entry point for all callbacks from the plugin
//==============================================================================
static inline std::unique_ptr<VSTPluginInstanceHeadless> createAndUpdateDesc (VSTPluginFormatHeadless& format, PluginDescription& desc)
{
    if (auto p = format.createInstanceFromDescription (desc, 44100.0, 512))
    {
        if (auto instance = dynamic_cast<VSTPluginInstanceHeadless*> (p.release()))
        {
           #if JUCE_MAC
            if (instance->vstModule->resFileId != 0)
                UseResFile (instance->vstModule->resFileId);
           #endif

            instance->fillInPluginDescription (desc);
            return std::unique_ptr<VSTPluginInstanceHeadless> (instance);
        }

        jassertfalse;
    }

    return {};
}

template <typename TypeToCreate>
static void createVstPluginInstance (VSTPluginFormatHeadless& format,
                                     const PluginDescription& desc,
                                     double sampleRate,
                                     int blockSize,
                                     AudioPluginFormat::PluginCreationCallback callback)
{
    std::unique_ptr<TypeToCreate> result;

    if (format.fileMightContainThisPluginType (desc.fileOrIdentifier))
    {
        File file (desc.fileOrIdentifier);

        auto previousWorkingDirectory = File::getCurrentWorkingDirectory();
        file.getParentDirectory().setAsCurrentWorkingDirectory();

        if (auto module = ModuleHandle::findOrCreateModule (file))
        {
            shellUIDToCreate = desc.uniqueId != 0 ? desc.uniqueId : desc.deprecatedUid;

            result = VSTPluginInstanceHeadless::create<TypeToCreate> (module, sampleRate, blockSize);

            if (result != nullptr && ! result->initialiseEffect (sampleRate, blockSize))
                result.reset();
        }

        previousWorkingDirectory.setAsCurrentWorkingDirectory();
    }

    String errorMsg;

    if (result == nullptr)
        errorMsg = TRANS ("Unable to load XXX plug-in file").replace ("XXX", "VST-2");

    callback (std::move (result), errorMsg);
}

template <typename TypeToCreate>
std::unique_ptr<AudioPluginInstance> createCustomVSTFromMainCallImpl (void* entryPointFunction,
                                                                      double initialSampleRate,
                                                                      int initialBufferSize)
{
    ModuleHandle::Ptr module = new ModuleHandle (File(), (MainCall) entryPointFunction);

    if (! module->open())
        return {};

    auto result = VSTPluginInstanceHeadless::create<TypeToCreate> (module, initialSampleRate, initialBufferSize);

    if (result == nullptr || ! result->initialiseEffect (initialSampleRate, initialBufferSize))
        return {};

    return result;
}

} // namespace juce

JUCE_END_IGNORE_DEPRECATION_WARNINGS
JUCE_END_IGNORE_WARNINGS_MSVC

#endif
