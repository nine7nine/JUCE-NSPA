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

/* JUCE-NSPA: winelib host needs Win32-registered threads to safely
 * call LoadLibraryW + the plugin's PE init code.  juce::Thread::launch
 * / std::thread give us plain pthreads that Wine has no record of;
 * any LoadLibraryW call from such a thread crashes with
 * "err:seh:NtRaiseException Exception frame is not in stack limits"
 * because Wine can't dispatch SEH on stacks it doesn't own.
 *
 * Re-use the WineWin32Dispatcher infrastructure from
 * juce_winelib_dispatch.h — same Win32 CreateThread + OleInitialize
 * + RT-priority promotion the per-instance VST3/VST2 workers use,
 * but a one-shot worker per async load instead of a long-lived
 * per-instance worker (the load IS the entire job — once it returns
 * the per-instance dispatcher takes over for the plugin's lifetime).
 *
 * Header is included unconditionally because the same call path runs
 * on non-winelib builds too — the dispatch types degrade to no-op
 * declarations when __WINE__ isn't defined (see the include's #if
 * gates at the top of juce_winelib_dispatch.h). */
#if JUCE_LINUX && defined (__WINE__)
 #include "../format_types/juce_winelib_dispatch.h"
#endif

namespace juce
{

AudioPluginFormat::AudioPluginFormat() {}
AudioPluginFormat::~AudioPluginFormat() {}

std::unique_ptr<AudioPluginInstance> AudioPluginFormat::createInstanceFromDescription (const PluginDescription& desc,
                                                                                       double initialSampleRate,
                                                                                       int initialBufferSize)
{
    String errorMessage;
    return createInstanceFromDescription (desc, initialSampleRate, initialBufferSize, errorMessage);
}

std::unique_ptr<AudioPluginInstance> AudioPluginFormat::createInstanceFromDescription (const PluginDescription& desc,
                                                                                       double initialSampleRate,
                                                                                       int initialBufferSize,
                                                                                       String& errorMessage)
{
    if (MessageManager::getInstance()->isThisTheMessageThread()
          && requiresUnblockedMessageThreadDuringCreation (desc))
    {
        errorMessage = NEEDS_TRANS ("This plug-in cannot be instantiated synchronously");
        return {};
    }

    WaitableEvent finishedSignal;
    std::unique_ptr<AudioPluginInstance> instance;

    auto callback = [&] (std::unique_ptr<AudioPluginInstance> p, const String& error)
    {
       errorMessage = error;
       instance = std::move (p);
       finishedSignal.signal();
    };

    if (! MessageManager::getInstance()->isThisTheMessageThread())
        createPluginInstanceAsync (desc, initialSampleRate, initialBufferSize, std::move (callback));
    else
        createPluginInstance (desc, initialSampleRate, initialBufferSize, std::move (callback));

    finishedSignal.wait();
    return instance;
}

struct AudioPluginFormat::AsyncCreateMessage final : public Message
{
    AsyncCreateMessage (const PluginDescription& d, double sr, int size, PluginCreationCallback call)
        : desc (d), sampleRate (sr), bufferSize (size), callbackToUse (std::move (call))
    {
    }

    PluginDescription desc;
    double sampleRate;
    int bufferSize;
    PluginCreationCallback callbackToUse;
};

#if JUCE_LINUX && defined (__WINE__)
/* JUCE-NSPA winelib helper: heap-allocated context for the one-shot
 * Win32 worker that runs an async plugin load.  Lives across the
 * Win32 thread's lifetime; the worker takes ownership via the
 * unique_ptr at entry and destroys it on return.  Kept namespace-
 * private to this TU — not exposed in the public API. */
namespace
{
    struct WineAsyncLoadCtx
    {
        AudioPluginFormat* self;
        PluginDescription  desc;
        double             sampleRate;
        int                bufferSize;
        AudioPluginFormat::PluginCreationCallback callback;
    };
}
#endif

void AudioPluginFormat::createPluginInstanceAsync (const PluginDescription& description,
                                                   double initialSampleRate, int initialBufferSize,
                                                   PluginCreationCallback callback)
{
    jassert (callback != nullptr);

#if JUCE_LINUX && defined (__WINE__)
    /* JUCE-NSPA winelib path: spawn a one-shot Win32 worker thread
     * via Wine's CreateThread (registers the thread's TEB / NT_TIB
     * with the Wine loader), run the synchronous createPluginInstance
     * on it, then re-dispatch the result onto the message thread via
     * MessageManager::callAsync to preserve the documented
     * "callback fires on the message thread" contract.
     *
     * Why not juce::Thread::launch: plain pthreads have no Wine
     * registration, so LoadLibraryW from such a thread crashes
     * mid-SEH-dispatch ("Exception frame is not in stack limits").
     *
     * Why not the existing WineWin32Dispatcher::run: that blocks
     * the caller until the worker finishes — same end result as
     * sync createPluginInstance.  We need fire-and-forget.
     *
     * The captureless trampoline takes the heap context, calls back
     * into the member function `createPluginInstance` which has
     * protected access; the lambda inherits this enclosing member
     * function's access privileges so the call type-checks. */
    auto* ctx = new WineAsyncLoadCtx {
        this,
        description,
        initialSampleRate,
        initialBufferSize,
        std::move (callback)
    };

    auto trampoline = +[] (void* p) -> WineDWORD __stdcall
    {
        std::unique_ptr<WineAsyncLoadCtx> c { static_cast<WineAsyncLoadCtx*> (p) };

        c->self->createPluginInstance (
            c->desc, c->sampleRate, c->bufferSize,
            [cb = std::move (c->callback)] (std::unique_ptr<AudioPluginInstance> instance,
                                             const String& errorMsg) mutable
            {
                MessageManager::callAsync (
                    [instance = std::move (instance), errorMsg,
                     cb = std::move (cb)]() mutable
                    {
                        cb (std::move (instance), errorMsg);
                    });
            });
        return 0;
    };

    WineDWORD threadId = 0;
    WineHANDLE workerHandle = CreateThread (nullptr, 0, trampoline, ctx, 0, &threadId);
    if (workerHandle != nullptr)
    {
        /* We don't join on the worker — it's fire-and-forget.  Close
         * the handle now; Wine reaps the thread when it exits. */
        CloseHandle (workerHandle);
        return;
    }

    /* CreateThread should never fail in practice, but if it does
     * salvage the callback from the leaked ctx and fall through to
     * the postMessage path so the caller still gets a result. */
    callback = std::move (ctx->callback);
    delete ctx;
#endif

    postMessage (new AsyncCreateMessage (description, initialSampleRate, initialBufferSize, std::move (callback)));
}

void AudioPluginFormat::handleMessage (const Message& message)
{
    if (auto m = dynamic_cast<const AsyncCreateMessage*> (&message))
        createPluginInstance (m->desc, m->sampleRate, m->bufferSize, std::move (m->callbackToUse));
}

} // namespace juce
