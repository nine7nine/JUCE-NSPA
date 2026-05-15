/* SPDX-License-Identifier: ISC
 *
 * juce_winelib_path.h — shared winelib path/file helpers for in-process
 * plugin hosts (VST3 + VST2).
 *
 * Extracted from juce_VST3PluginFormatImpl.h's inline copies so VST2's
 * impl can share one PE detect + DOS-path conversion path with VST3.
 *
 * Not wrapped in namespace juce — included from inside JUCE's
 * `namespace juce { ... }` block in the host impls; absorbs that
 * namespace so the symbols end up at `juce::*` automatically.
 *
 * IMPORTANT: this header is included from INSIDE `namespace juce { }`
 * blocks.  System headers (`<cstdio>`, `<cstdlib>`, `<vector>`, etc.)
 * MUST NOT be `#include`d here — doing so parses libstdc++'s
 * `namespace std { ... }` declarations inside `juce::`, producing
 * `juce::std::vector` etc. which then fail to resolve at use sites.
 * Use juce primitives only (juce::HeapBlock, juce::String,
 * juce::SystemStats::getEnvironmentVariable, etc.); they're available
 * because juce_core's headers have already been processed before this
 * header is included.
 *
 * Functions:
 *   winelib_is_pe_file       — magic-bytes PE detect (MZ).
 *   winelib_unix_to_dos_path — Linux path → Wine DOS path (X:\...) via
 *                              $WINEPREFIX/dosdevices/ enumeration.
 *   winelib_path_to_utf16    — ASCII-only DOS path → uint16 buffer for
 *                              LoadLibraryW (Wine's 16-bit WCHAR).
 *                              Returned as juce::HeapBlock<juce::uint16>
 *                              owning the NUL-terminated buffer.
 *
 * Phase 3's winelib_log debug appender lived alongside these helpers in
 * juce_VST3PluginFormatImpl.h; it pulled in <cstdio> + <cstdarg> which
 * tripped the nested-namespace trap above when this header was first
 * extracted.  Dropped here — re-add as needed via a TU-local helper
 * file outside namespace juce.
 */

#pragma once

/* No-op stub for the Phase 3 debug logger.  Variadic template so the
 * existing printf-style call sites compile unchanged.  When debug
 * logging is needed again, swap this for a juce::Logger::writeToLog
 * helper that takes a juce::String — but that requires reformulating
 * the call sites, hence keeping the no-op as the default. */
template <typename... Args>
inline void winelib_log (const char*, Args&&...) noexcept {}

inline bool winelib_is_pe_file (const File& f)
{
    FileInputStream stream (f);
    if (! stream.openedOk()) return false;
    char magic[2] = { 0, 0 };
    return stream.read (magic, 2) == 2 && magic[0] == 'M' && magic[1] == 'Z';
}

/* Convert a Linux filesystem path to a Wine DOS path (e.g. "C:\\...")
 * BEFORE LoadLibraryW.  Without this, Wine stores the raw NT-namespace
 * "\\?\\unix\\<host-path>" as the module's FullDllName.  Plugins that
 * introspect their location via GetModuleFileNameW() (u-he VST3s in
 * particular) can't parse that, log "ERROR: Could not find binary
 * path", and crash downstream when they try to load related resources
 * (preset banks, midi-assign tables, etc.) relative to their binary.
 *
 * Ported from yabridge's src/wine-host/utils.cpp::to_dos_path()
 * (Robbert van der Helm, GPL-3).  We use only the manual-dosdevices
 * fallback path because Wine's `wine_get_dos_file_name()` empirically
 * only canonicalizes `.dll/.exe/.sys/.drv` extensions — for `.vst3`
 * (our case) it returns the unusable `\\?\\unix\\...` form.
 *
 * Walks $WINEPREFIX/dosdevices/ for drive-letter symlinks (X:), finds
 * the one whose target is the longest unix prefix of unixPath, and
 * rewrites the path as `X:\\relative\\with\\backslashes`.
 */
inline String winelib_unix_to_dos_path (const String& unixPath)
{
    const String prefixEnv = SystemStats::getEnvironmentVariable ("WINEPREFIX", {});
    if (prefixEnv.isEmpty())
        return unixPath;

    const File dosDevicesDir = File (prefixEnv).getChildFile ("dosdevices");
    if (! dosDevicesDir.isDirectory())
        return unixPath;

    const String unixPathStr = unixPath;
    String bestLetter;
    int bestMatchLen = 0;

    for (const auto& entry : RangedDirectoryIterator (dosDevicesDir, false, "*", File::findDirectories))
    {
        const String name = entry.getFile().getFileName();
        // Drive letters are exactly "X:" — skip "X::" block-device aliases.
        if (name.length() != 2 || name[1] != ':'
            || ! CharacterFunctions::isLetter (name[0]))
            continue;

        // Resolve symlink to its canonical target.
        const File target = entry.getFile().getLinkedTarget();
        String targetStr = target.getFullPathName();
        if (targetStr.isEmpty())
            continue;
        if (! targetStr.endsWithChar ('/'))
            targetStr += '/';

        if (unixPathStr.length() >= targetStr.length()
            && unixPathStr.substring (0, targetStr.length()) == targetStr
            && targetStr.length() > (size_t) bestMatchLen)
        {
            bestLetter = String::charToString (CharacterFunctions::toUpperCase (name[0]));
            bestMatchLen = (int) targetStr.length();
        }
    }

    if (bestMatchLen == 0)
        return unixPath;

    String dos = bestLetter + ":\\" + unixPathStr.substring (bestMatchLen);
    return dos.replaceCharacter ('/', '\\');
}

/* ASCII-only conversion; sufficient for VST install paths under Wine.
 * Real UTF-8 → UTF-16 would need surrogate pair handling.  Returns a
 * juce::HeapBlock<juce::uint16> owning a NUL-terminated buffer; the
 * caller passes `.getData()` to LoadLibraryW.  We use HeapBlock rather
 * than std::vector to avoid pulling <vector> into this header (see
 * top-of-file comment on the namespace-nesting trap). */
inline HeapBlock<juce::uint16> winelib_path_to_utf16 (const String& path)
{
    auto* utf8 = path.toRawUTF8();
    int len = 0;
    for (const char* p = utf8; *p != 0; ++p)
        ++len;

    HeapBlock<juce::uint16> out (len + 1);
    for (int i = 0; i < len; ++i)
        out[i] = (juce::uint16) (unsigned char) utf8[i];
    out[len] = 0;
    return out;
}
