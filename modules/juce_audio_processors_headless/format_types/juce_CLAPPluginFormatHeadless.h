/* SPDX-License-Identifier: ISC
 *
 * juce_CLAPPluginFormatHeadless.h — CLAP plugin format declaration
 * (winelib-only on this fork).
 *
 * Loads Windows .clap PE DLLs via Wine's LoadLibraryW.  There is no
 * Linux .so CLAP loader path — clap plugins targeted at native Linux
 * have a different ABI/runtime profile and aren't accepted by this
 * format.
 *
 * Mirrors the VST3PluginFormatHeadless split:
 *   - This headless variant implements the full scan + AudioPluginFormat
 *     contract.
 *   - The non-headless juce_CLAPPluginFormat in juce_audio_processors
 *     thin-overrides createPluginInstance to participate in editor
 *     window plumbing.
 */

namespace juce
{

#if JUCE_INTERNAL_HAS_CLAP

/**
    Implements a plugin format for CLAP plugins (winelib-only on this fork).

    @tags{Audio}
*/
class JUCE_API CLAPPluginFormatHeadless   : public AudioPluginFormat
{
public:
    CLAPPluginFormatHeadless() = default;

    //==============================================================================
    static String getFormatName()                   { return "CLAP"; }
    String getName() const override                 { return getFormatName(); }
    bool canScanForPlugins() const override         { return true; }
    bool isTrivialToScan() const override           { return false; }

    void findAllTypesForFile (OwnedArray<PluginDescription>&, const String& fileOrIdentifier) override;
    bool fileMightContainThisPluginType (const String& fileOrIdentifier) override;
    String getNameOfPluginFromIdentifier (const String& fileOrIdentifier) override;
    bool pluginNeedsRescanning (const PluginDescription&) override;
    StringArray searchPathsForPlugins (const FileSearchPath&, bool recursive, bool) override;
    bool doesPluginStillExist (const PluginDescription&) override;
    FileSearchPath getDefaultLocationsToSearch() override;

protected:
    //==============================================================================
    // createPluginInstance is protected (not private as in VST3PluginFormatHeadless)
    // because our GUI subclass juce::CLAPPluginFormat thin-defers to it until
    // Phase H wires the X11 embed bridge.  VST3 splits the impl through a
    // separate template helper so its base can stay private.
    void createPluginInstance (const PluginDescription&, double initialSampleRate,
                               int initialBufferSize, PluginCreationCallback) override;
    bool requiresUnblockedMessageThreadDuringCreation (const PluginDescription&) const override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CLAPPluginFormatHeadless)
};

#endif

} // namespace juce
