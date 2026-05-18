/* SPDX-License-Identifier: ISC
 *
 * juce_CLAPPluginFormat.h — CLAP plugin format with GUI/editor support
 * (winelib-only on this fork).  Thin subclass of CLAPPluginFormatHeadless
 * that overrides createPluginInstance to participate in the host's
 * editor window plumbing.  Mirrors the VST3PluginFormat / VST3PluginFormatHeadless
 * split.
 */

namespace juce
{

#if JUCE_INTERNAL_HAS_CLAP

/**
    Implements a CLAP plugin format with GUI support (winelib-only on this fork).

    @tags{Audio}
*/
class JUCE_API CLAPPluginFormat : public CLAPPluginFormatHeadless
{
    void createPluginInstance (const PluginDescription&, double, int, PluginCreationCallback) override;
};

#endif

} // namespace juce
