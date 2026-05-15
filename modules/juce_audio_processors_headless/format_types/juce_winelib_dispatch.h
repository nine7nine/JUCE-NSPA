/* SPDX-License-Identifier: ISC
 *
 * juce_winelib_dispatch.h — shared Win32-thread dispatcher for in-process
 * plugin hosts (VST3 + VST2) running as winelib ELFs.
 *
 * When a winelib ELF (compiled in JUCE_LINUX mode) calls into Win32 PE
 * plugin code, every call must originate from a thread created by Win32
 * CreateThread, NOT a JUCE-promoted pthread.  pthreads don't have the
 * per-thread Win32 TEB / TLS / COM-apartment state that Win32 PE
 * libraries assume.  When plugin code does TLS lookups, COM apartment
 * checks, or thread-local allocations against missing state, it
 * eventually hits an unrecoverable condition and calls ExitProcess() —
 * which is exactly the death pattern our diagnostics caught
 * (LdrShutdownProcess from inside the plugin without any prior signal /
 * abort / C++ exception).
 *
 * Solution: one dedicated Win32 worker thread per plugin instance,
 * owned by the format's per-instance holder/container.  All
 * plugin-touching calls for that instance (createInstance / load /
 * initialize / setActive / process / terminate) marshal onto the
 * instance's worker.  Plugin sees one stable Win32 thread identity for
 * its entire lifetime; N instances run on N parallel workers.
 *
 * Originally defined inline inside juce_VST3PluginFormatImpl.h;
 * extracted here so juce_VSTPluginFormatImpl.h (VST2) can share the
 * same type instead of duplicating the implementation.
 *
 * Not wrapped in namespace juce — included from inside JUCE's
 * `namespace juce { ... }` block in the host impls; absorbs that
 * namespace so the symbols end up at `juce::*` automatically.
 *
 * Symbols introduced:
 *   extern "C" decls for the Win32 API surface we use (LoadLibraryW,
 *     CreateThread, WaitForSingleObject, CloseHandle, OleInitialize,
 *     OleUninitialize, GetCurrentThread, SetThreadPriority).
 *   WINELIB_INFINITE / WINELIB_THREAD_TIME_CRITICAL constants.
 *   class WineWin32Dispatcher — the worker + run() handle.
 *   globalWineDispatcher() — singleton used exclusively for one-time
 *     LoadLibraryW per PE binary.  Per-instance work uses an instance
 *     member.
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <thread>

#include "juce_winelib_pi_sync.h"

extern "C" {
    /* Inline declarations — avoids pulling <windows.h>, which would
     * collide with libstdc++ in JUCE_LINUX mode.  Note: WCHAR strings
     * use `uint16_t*`, NOT `wchar_t*`, because winegcc's
     * `-fno-short-wchar` (set by our toolchain) makes wchar_t 32-bit
     * but Wine's PE side expects 16-bit WCHAR. */
    void*       __stdcall LoadLibraryW   (const uint16_t* lpLibFileName);
    void*       __stdcall GetProcAddress (void* hModule, const char* lpProcName);
    int         __stdcall FreeLibrary    (void* hModule);

    using WineHANDLE   = void*;
    using WineDWORD    = uint32_t;
    using WineBOOL     = int;
    using WineThreadFunc = WineDWORD (__stdcall*) (void* param);

    WineHANDLE __stdcall CreateThread        (void*           securityAttrs,
                                              size_t          stackSize,
                                              WineThreadFunc  startRoutine,
                                              void*           param,
                                              WineDWORD       creationFlags,
                                              WineDWORD*      outThreadId);
    WineDWORD  __stdcall WaitForSingleObject (WineHANDLE handle, WineDWORD ms);
    WineBOOL   __stdcall CloseHandle         (WineHANDLE handle);
    int        __stdcall OleInitialize       (void* reserved);
    void       __stdcall OleUninitialize     (void);

    // RT priority promotion — see yabridge/src/wine-host/nspa_rt.h.
    // SetThreadPriority(TIME_CRITICAL) routes through ntdll's
    // NtSetInformationThread(ThreadBasePriority) handler, which under
    // NSPA resolves to SCHED_FIFO at NSPA_RT_PRIO.
    WineHANDLE __stdcall GetCurrentThread    (void);
    WineBOOL   __stdcall SetThreadPriority   (WineHANDLE handle, int priority);
}

constexpr WineDWORD WINELIB_INFINITE             = 0xFFFFFFFFu;
constexpr int       WINELIB_THREAD_TIME_CRITICAL = 15;  // THREAD_PRIORITY_TIME_CRITICAL

class WineWin32Dispatcher
{
public:
    WineWin32Dispatcher()
    {
        workerThread = CreateThread (nullptr, 0, &workerEntry, this, 0, nullptr);
    }

    ~WineWin32Dispatcher()
    {
        {
            std::lock_guard<PiMutex> lk (mtx);
            shutdown = true;
            cvRequest.broadcast (mtx);
        }
        if (workerThread != nullptr)
        {
            WaitForSingleObject (workerThread, WINELIB_INFINITE);
            CloseHandle (workerThread);
            workerThread = nullptr;
        }
    }

    WineWin32Dispatcher (const WineWin32Dispatcher&)            = delete;
    WineWin32Dispatcher& operator= (const WineWin32Dispatcher&) = delete;

    /** Run `fn` on the Win32 worker thread, block until complete.
        Caller's return value (if any) should be captured by reference
        inside the lambda — e.g.
            tresult r;
            dispatcher.run ([&] { r = plugin->setActive (true); });
    */
    void run (std::function<void()> fn)
    {
        if (workerThread == nullptr)
        {
            // Fallback if CreateThread failed (shouldn't happen).
            fn();
            return;
        }
        // Self-dispatch guard: if a dispatched call recursively calls
        // run() (e.g. a wrapped lifecycle helper that internally invokes
        // dispatch on another call site), don't deadlock — run inline
        // on the worker itself.  Without this guard the worker would
        // block in cvDone.wait while being the only thread that can
        // clear taskPending.
        if (std::this_thread::get_id() == workerThreadId)
        {
            fn();
            return;
        }
        std::unique_lock<PiMutex> lk (mtx);
        currentTask = std::move (fn);
        taskPending = true;
        cvRequest.signal (mtx);
        while (taskPending)
            cvDone.wait (mtx);
    }

private:
    static WineDWORD __stdcall workerEntry (void* selfPtr)
    {
        // Initialize OLE on the worker thread itself.  Without this,
        // plugin's internal COM/RPC marshalling falls back to whichever
        // OTHER thread happens to have OleInitialize'd state — usually
        // the host's main thread (pthread, incomplete TEB), tripping
        // the ExitProcess path.  Mirror of yabridge's host.cpp:112 but
        // on the worker thread instead of the host main.
        OleInitialize (nullptr);

        // Promote to NSPA's RT band.  SetThreadPriority(TIME_CRITICAL)
        // routes through ntdll's NtSetInformationThread handler under
        // NSPA, which maps to SCHED_FIFO at NSPA_RT_PRIO.  Mirror of
        // yabridge::nspa::set_thread_time_critical (nspa_rt.h:71).
        // Plugin worker pools spawned during PROCESS_ATTACH / DllMain /
        // pluginCreate inherit the creator's scheduling — without this
        // promotion u-he's boost::thread workers throw
        // boost::thread_resource_error when the kernel refuses
        // pthread_create's RT request from a non-RT parent.  Process
        // priority class is already REALTIME via Element's
        // WinelibPluginHostInit (src/main.cc).
        SetThreadPriority (GetCurrentThread(), WINELIB_THREAD_TIME_CRITICAL);

        auto* self = static_cast<WineWin32Dispatcher*> (selfPtr);
        self->workerThreadId = std::this_thread::get_id();
        self->workerLoop();
        OleUninitialize();
        return 0;
    }

    void workerLoop()
    {
        while (true)
        {
            std::function<void()> task;
            {
                std::unique_lock<PiMutex> lk (mtx);
                while (! taskPending && ! shutdown)
                    cvRequest.wait (mtx);
                if (shutdown && ! taskPending)
                    return;
                task = std::move (currentTask);
            }

            task();

            {
                std::lock_guard<PiMutex> lk (mtx);
                taskPending = false;
                cvDone.signal (mtx);
            }
        }
    }

    WineHANDLE             workerThread = nullptr;
    std::thread::id        workerThreadId{};
    PiMutex                mtx;
    PiCond                 cvRequest;
    PiCond                 cvDone;
    std::function<void()>  currentTask;
    bool                   taskPending = false;
    bool                   shutdown    = false;
};

/* Global Win32 dispatcher — used EXCLUSIVELY for the one-time
 * LoadLibraryW per PE binary.  DllMain runs once per DLL regardless of
 * how many plugin instances later use it.  Keeping LoadLibraryW on a
 * stable Win32 thread means the plugin's PROCESS_ATTACH executes in
 * proper Win32 context; later per-instance CreateThread calls deliver
 * DLL_THREAD_ATTACH callbacks to the plugin's DllMain for the new
 * worker threads automatically.
 *
 * All per-instance plugin operations go to the per-instance worker on
 * the format's per-instance holder/container.
 */
inline WineWin32Dispatcher& globalWineDispatcher()
{
    static WineWin32Dispatcher instance;
    return instance;
}
