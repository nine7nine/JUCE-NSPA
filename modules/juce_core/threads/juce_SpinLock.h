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

//==============================================================================
/**
    A simple spin-lock class that can be used as a simple, low-overhead mutex for
    uncontended situations.

    Note that unlike a CriticalSection, this type of lock is not re-entrant, and may
    be less efficient when used in a highly contended situation, but it's very small and
    requires almost no initialisation.
    It's most appropriate for simple situations where you're only going to hold the
    lock for a very brief time.

    @see CriticalSection

    @tags{Core}
*/
class JUCE_API  SpinLock
{
public:
   #if defined (__WINE__)
    /* librtpi-backed non-recursive PI mutex.  pi_mutex_init zeros the
     * struct and stores flags=0; safe to call from a static ctor.  See
     * comment on the `lock` member below for why we swap away from the
     * upstream spin-yield loop under __WINE__. */
    SpinLock()  noexcept { pi_mutex_init    (&lock, 0); }
    ~SpinLock() noexcept { pi_mutex_destroy (&lock); }
   #else
    inline SpinLock() = default;
    inline ~SpinLock() = default;
   #endif

    /** Acquires the lock.
        This will block until the lock has been successfully acquired by this thread.
        Note that a SpinLock is NOT re-entrant, and is not smart enough to know whether the
        caller thread already has the lock - so if a thread tries to acquire a lock that it
        already holds, this method will never return!

        It's strongly recommended that you never call this method directly - instead use the
        ScopedLockType class to manage the locking using an RAII pattern instead.
    */
    void enter() const noexcept;

    /** Attempts to acquire the lock, returning true if this was successful. */
    inline bool tryEnter() const noexcept
    {
       #if defined (__WINE__)
        return pi_mutex_trylock (&lock) == 0;
       #else
        return lock.compareAndSetBool (1, 0);
       #endif
    }

    /** Releases the lock. */
    inline void exit() const noexcept
    {
       #if defined (__WINE__)
        /* pi_mutex_unlock self-validates ownership (returns EPERM on
         * TID mismatch).  No need for the upstream `lock.get() == 1`
         * jassert — the kernel catches misuse at runtime. */
        pi_mutex_unlock (&lock);
       #else
        jassert (lock.get() == 1); // Agh! Releasing a lock that isn't currently held!
        lock = 0;
       #endif
    }

    //==============================================================================
    /** Provides the type of scoped lock to use for locking a SpinLock. */
    using ScopedLockType = GenericScopedLock<SpinLock>;

    /** Provides the type of scoped unlocker to use with a SpinLock. */
    using ScopedUnlockType = GenericScopedUnlock<SpinLock>;

    /** Provides the type of scoped try-lock to use for locking a SpinLock. */
    using ScopedTryLockType = GenericScopedTryLock<SpinLock>;

private:
    //==============================================================================
   #if defined (__WINE__)
    /* librtpi-backed non-recursive PI mutex.  Replaces the upstream
     * Atomic<int> spin-yield path because, under SCHED_FIFO@80 on
     * PREEMPT_RT, the upstream loop:
     *
     *     for (int i = 20; --i >= 0;) if (tryEnter()) return;
     *     while (! tryEnter()) Thread::yield();
     *
     * starves the holder — Thread::yield() yields only to higher-prio
     * threads, and a same-prio FIFO holder on the same CPU cannot make
     * progress while the spinner consumes the CPU at the same priority.
     *
     * pi_mutex_lock instead deschedules the contender and stores the
     * holder's TID in the futex word, letting the kernel boost the
     * holder if needed (FUTEX_LOCK_PI) so it can release.  Uncontended
     * cost is identical (one CAS); contended cost trades a ~500ns
     * syscall round-trip for correctness (no more same-prio starvation
     * deadlock).  See modules/juce_core/native/juce_winelib_rtpi.h. */
    mutable pi_mutex_t lock;
   #else
    mutable Atomic<int> lock;
   #endif

    JUCE_DECLARE_NON_COPYABLE (SpinLock)
};

} // namespace juce
