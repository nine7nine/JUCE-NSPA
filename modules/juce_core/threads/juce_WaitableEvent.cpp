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

namespace juce
{

WaitableEvent::WaitableEvent (bool manualReset) noexcept
    : useManualReset (manualReset)
{
}

bool WaitableEvent::wait (double timeOutMilliseconds) const
{
   #if defined (__WINE__)
    /* librtpi-backed wait.  Same auto-reset / manual-reset contract as
     * the upstream std::cv path: predicate loop handles spurious wakes
     * and the broadcast()-wake-multiple race in auto-reset mode
     * (multiple waiters wake, first to acquire the mutex sees
     * triggered=true and clears it inside the held lock; subsequent
     * waiters re-check and re-wait).
     *
     * Note the loop structure: triggered is re-checked at the TOP of
     * each iteration, after any wake.  This collapses three cases —
     * legitimate signal, spurious wake, and timeout-with-signal-race
     * (signal arrives just as the kernel decides we've timed out) —
     * into the same code path.  Without that, the timeout-with-signal
     * race would drop the signal and return false. */
    std::unique_lock<PiMutex> lock (mutex);

    if (timeOutMilliseconds < 0.0)
    {
        while (! triggered.load())
            condition.wait (mutex);
    }
    else
    {
        /* Absolute deadline captured once.  Each iteration recomputes
         * remaining so spurious wakes can't reset the timeout (matches
         * std::condition_variable::wait_for with a predicate, which is
         * implemented in terms of wait_until under an absolute
         * deadline). */
        const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::duration<double, std::milli> { timeOutMilliseconds };
        while (! triggered.load())
        {
            const auto remaining = deadline - std::chrono::steady_clock::now();
            if (remaining <= std::chrono::steady_clock::duration::zero())
                return false;
            condition.timed_wait (mutex, remaining);
        }
    }

    if (! useManualReset)
        reset();

    return true;
   #else
    std::unique_lock<std::mutex> lock (mutex);

    if (! triggered)
    {
        if (timeOutMilliseconds < 0.0)
        {
            condition.wait (lock, [this] { return triggered == true; });
        }
        else
        {
            if (! condition.wait_for (lock, std::chrono::duration<double, std::milli> { timeOutMilliseconds },
                                      [this] { return triggered == true; }))
            {
                return false;
            }
        }
    }

    if (! useManualReset)
        reset();

    return true;
   #endif
}

void WaitableEvent::signal() const
{
   #if defined (__WINE__)
    /* broadcast() (vs signal()) mirrors upstream's notify_all so
     * behaviour is identical for callers; auto-reset's one-waiter
     * semantics fall out of the predicate loop in wait(). */
    std::lock_guard<PiMutex> lock (mutex);

    triggered.store (true);
    condition.broadcast (mutex);
   #else
    std::lock_guard<std::mutex> lock (mutex);

    triggered = true;
    condition.notify_all();
   #endif
}

void WaitableEvent::reset() const
{
    triggered = false;
}

} // namespace juce
