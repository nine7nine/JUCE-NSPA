/* SPDX-License-Identifier: ISC
 *
 * juce_clap_winelib_abi.h — CLAP function-pointer ABI override for
 * winelib hosts.
 *
 * Upstream clap-headers define CLAP_ABI as __cdecl on Windows and as
 * nothing on every other platform.  When we build JUCE as a winelib ELF
 * via the JUCE-as-Linux pivot (__WINE__ defined, JUCE_LINUX still on),
 * upstream macros.h selects the empty (non-Windows) definition.
 *
 * The .clap files we load via LoadLibraryW are Windows PE DLLs whose
 * function-pointer fields were compiled with __cdecl on x86_64 →
 * Microsoft x64 calling convention.  Calling those from a System V
 * x86_64 host without an ABI override produces register/stack mismatch
 * and immediate crashes on first invocation.
 *
 * Same problem class as VST3's PLUGIN_API, solved the same way:
 * pre-define CLAP_ABI so upstream macros.h sees it already-set and
 * skips its own default.
 *
 * This header must be included BEFORE any upstream clap header to
 * take effect.
 */

#pragma once

#if defined (__WINE__) && (defined (JUCE_LINUX) || defined (JUCE_BSD)) && defined (__x86_64__)
 #ifndef CLAP_ABI
  #define CLAP_ABI __attribute__ ((ms_abi))
 #endif
#endif

#include <clap/clap.h>
