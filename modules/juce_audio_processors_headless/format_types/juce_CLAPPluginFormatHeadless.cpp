/* SPDX-License-Identifier: ISC
 *
 * juce_CLAPPluginFormatHeadless.cpp — CLAP plugin format implementation
 * (winelib-only on this fork).
 *
 * Phase B: AudioPluginFormat skeleton + WINEPREFIX scan path.
 * Phase C: DLL load + factory enumerate + descriptor → PluginDescription.
 * Phase D: CLAPPluginInstance lifecycle (silent passthrough).
 *
 * createPluginInstance is still a no-op pending Phase D; everything else
 * — scan, DLL load, factory iteration, description population — is live.
 */

#if JUCE_INTERNAL_HAS_CLAP

// CLAP headers at global namespace — must precede `namespace juce { }`
// so `clap_plugin_t` / `clap_plugin_factory_t` / ... aren't pulled into
// the juce:: namespace.  juce_clap_winelib_abi.h applies our ms_abi
// override before #include <clap/clap.h>.
#include "clap/juce_clap_winelib_abi.h"

namespace juce
{

#include "juce_CLAPPluginFormatImpl.h"

static void searchOneDir (CLAPPluginFormatHeadless& format, StringArray& results,
                          const File& dir, bool recursive)
{
    if (! dir.isDirectory())
        return;

    for (const auto& iter : RangedDirectoryIterator (dir, false, "*", File::findFilesAndDirectories))
    {
        const auto child = iter.getFile();

        if (format.fileMightContainThisPluginType (child.getFullPathName()))
            results.add (child.getFullPathName());
        else if (recursive && child.isDirectory())
            searchOneDir (format, results, child, true);
    }
}

void CLAPPluginFormatHeadless::findAllTypesForFile (OwnedArray<PluginDescription>& results,
                                                    const String& fileOrIdentifier)
{
    winelib_log ("[CLAPPluginFormat] findAllTypesForFile('%s')",
                 fileOrIdentifier.toRawUTF8());

    if (! fileMightContainThisPluginType (fileOrIdentifier))
    {
        winelib_log ("[CLAPPluginFormat] '%s' rejected by fileMightContainThisPluginType",
                     fileOrIdentifier.toRawUTF8());
        return;
    }

    findDescriptionsForFile (results, File (fileOrIdentifier));
    winelib_log ("[CLAPPluginFormat] findAllTypesForFile returned %d descriptions",
                 results.size());
}

bool CLAPPluginFormatHeadless::fileMightContainThisPluginType (const String& fileOrIdentifier)
{
    auto f = File::createFileWithoutCheckingPath (fileOrIdentifier);
    return f.hasFileExtension (".clap") && f.exists();
}

String CLAPPluginFormatHeadless::getNameOfPluginFromIdentifier (const String& fileOrIdentifier)
{
    // CLAP plugin names come from the factory descriptor.  Without loading
    // the DLL we can only return the filename; the descriptor name is
    // filled in by Phase C scan.
    return File (fileOrIdentifier).getFileNameWithoutExtension();
}

bool CLAPPluginFormatHeadless::pluginNeedsRescanning (const PluginDescription& description)
{
    return File (description.fileOrIdentifier).getLastModificationTime() != description.lastFileModTime;
}

bool CLAPPluginFormatHeadless::doesPluginStillExist (const PluginDescription& description)
{
    return File (description.fileOrIdentifier).exists();
}

StringArray CLAPPluginFormatHeadless::searchPathsForPlugins (const FileSearchPath& directoriesToSearch,
                                                             const bool recursive, bool)
{
    StringArray results;

    for (int i = 0; i < directoriesToSearch.getNumPaths(); ++i)
        searchOneDir (*this, results, directoriesToSearch[i], recursive);

    return results;
}

FileSearchPath CLAPPluginFormatHeadless::getDefaultLocationsToSearch()
{
   #if defined (__WINE__) && (JUCE_LINUX || JUCE_BSD)
    // Winelib host: derive Windows-style CLAP paths from WINEPREFIX.
    //
    // The CLAP spec (entry.h) defines two Windows search dirs:
    //   %COMMONPROGRAMFILES%\CLAP
    //      → "C:\Program Files\Common Files\CLAP"
    //   %LOCALAPPDATA%\Programs\Common\CLAP
    //      → "C:\Users\<user>\AppData\Local\Programs\Common\CLAP"
    //
    // Under Wine those live inside the prefix's drive_c tree.  Same shape
    // as the VST3 winelib path block in juce_VST3PluginFormatHeadless.cpp.
    if (const char* prefix = getenv ("WINEPREFIX"))
    {
        const String p (prefix);
        const char* userEnv = getenv ("USER");
        const String user (userEnv != nullptr ? userEnv : "");
        return FileSearchPath (p + "/drive_c/Program Files/Common Files/CLAP;"
                             + p + "/drive_c/users/" + user
                             + "/AppData/Local/Programs/Common/CLAP");
    }
    return FileSearchPath();
   #else
    // Non-winelib builds: the format is gated off in PluginFormatDefs.h,
    // so this branch is unreachable.  Return an empty path defensively.
    return FileSearchPath();
   #endif
}

void CLAPPluginFormatHeadless::createPluginInstance (const PluginDescription& description,
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

    // Re-derive the plugin id from the module's factory by matching
    // PluginDescription::uniqueId (hash of clap id) against the factory's
    // descriptor list.  This avoids storing the raw id on PluginDescription
    // (CLAP ids are reverse-DNS strings, not numeric — JUCE's uniqueId is
    // int).
    const auto* factory = module->getFactory();
    if (factory == nullptr)
    {
        callback (nullptr, "CLAP factory unavailable");
        return;
    }

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

    auto instance = CLAPPluginInstance::create (module, description, matchedClapId);

    if (instance == nullptr)
    {
        callback (nullptr, "CLAP instance creation failed: " + description.name);
        return;
    }

    instance->setRateAndBufferSizeDetails (initialSampleRate, initialBufferSize);
    callback (std::move (instance), {});
}

bool CLAPPluginFormatHeadless::requiresUnblockedMessageThreadDuringCreation (const PluginDescription&) const
{
    return false;
}

} // namespace juce

#endif
