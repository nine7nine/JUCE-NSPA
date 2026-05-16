/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * juce_winelib_pi_sync.h — C++ RAII wrappers over the vendored librtpi
 * primitives in juce_winelib_rtpi.h.
 *
 * Same C++ RAII wrapper shape I authored for yabridge-nspa as
 * `nspa: PiMutex / PiCond C++ RAII wrappers` (191ab148).  The
 * wrappers themselves originated in my Wine-NSPA work over librtpi
 * and were carried into yabridge and now JUCE on the same shape.
 * Both process-private and process-shared modes are supported:
 *
 *   PiMutex()             — process-private (FUTEX_LOCK_PI_PRIVATE)
 *   PiMutex(PiShared{})   — process-shared  (FUTEX_LOCK_PI), suitable
 *                           for placement-new into a shmem region
 *                           shared with another wine process or with
 *                           Element's native side.
 *
 * The in-process JUCE host only needs the private flavour today, but
 * keeping shmem support adds zero runtime cost (a flag bit) and leaves
 * the door open for an Element ↔ winelib boundary swap to PI shmem
 * rendezvous later — same surface my yabridge-nspa L2 audio path uses.
 *
 * Why these wrappers rather than std::mutex / std::condition_variable:
 *
 *   - PI inheritance: a high-prio thread blocking on a PiMutex held by a
 *     lower-prio thread temporarily boosts the lower-prio thread to the
 *     waiter's prio.  std::mutex (pthread_mutex_t with PRIO_NONE) does
 *     not — yielding classic priority inversion under SCHED_FIFO@80.
 *
 *   - PiCond uses FUTEX_WAIT_REQUEUE_PI / FUTEX_CMP_REQUEUE_PI so the
 *     wake-and-reacquire is atomic w.r.t. PI ordering.  Plain
 *     std::condition_variable leaves a window between wake and lock
 *     where the woken thread can be preempted, breaking the PI chain
 *     until it manages to reacquire.
 *
 * Use exactly as you would std::mutex + std::condition_variable_any:
 *
 *     PiMutex mtx;
 *     PiCond  cv;
 *
 *     // producer
 *     {
 *         std::lock_guard<PiMutex> lk(mtx);
 *         done = true;
 *         cv.signal(mtx);              // single waiter
 *     }
 *
 *     // consumer
 *     {
 *         std::unique_lock<PiMutex> lk(mtx);
 *         while (!done)
 *             cv.wait(mtx);            // releases lk, reacquires on wake
 *     }
 *
 * Lifetime note for shmem regions: the destructor calls pi_mutex_destroy
 * (memset zero).  When placement-new'd into shmem, do not invoke the
 * destructor from a process that may still have peers using the region;
 * coordinate teardown explicitly.
 */

#pragma once

#include <chrono>
#include <ctime>
#include <stdexcept>
#include <system_error>

#include "juce_winelib_rtpi.h"

// This header is intentionally NOT wrapped in `namespace juce { ... }`.
// It is designed to be #included from inside an already-open namespace
// (the JUCE module headers are structured as one big `namespace juce
// { ... #include ... }`, so wrapping here would nest to `juce::juce`).
// The symbols below absorb the includer's namespace.  For the JUCE
// VST3 host that's `juce::` — PiMutex / PiCond / PiShared end up as
// juce::PiMutex etc.

// Tag type to construct a process-shared pi primitive.
// Use as PiMutex(PiShared{}) / PiCond(PiShared{}).
struct PiShared {};

/* RAII wrapper around pi_mutex_t.  BasicLockable + Lockable — usable
 * with std::lock_guard, std::unique_lock, std::scoped_lock.
 *
 * Non-copyable, non-movable: the underlying pi_mutex_t holds a futex
 * word whose address must remain stable for the lifetime of the
 * primitive (kernel waiter queues are keyed by physical address for
 * PSHARED, virtual address otherwise).
 */
class PiMutex
{
public:
    PiMutex() noexcept
    {
        // flags=0: process-private, non-recursive.  pi_mutex_init's
        // only failure path is EINVAL on unknown flags, which flags=0
        // cannot trigger.
        pi_mutex_init (&m_, 0);
    }

    explicit PiMutex (PiShared /*tag*/) noexcept
    {
        pi_mutex_init (&m_, RTPI_MUTEX_PSHARED);
    }

    ~PiMutex() noexcept
    {
        pi_mutex_destroy (&m_);
    }

    PiMutex (const PiMutex&)            = delete;
    PiMutex& operator= (const PiMutex&) = delete;
    PiMutex (PiMutex&&)                 = delete;
    PiMutex& operator= (PiMutex&&)      = delete;

    void lock()
    {
        const int err = pi_mutex_lock (&m_);
        if (err != 0)
            throw std::system_error (err, std::system_category(), "pi_mutex_lock");
    }

    // Unlock is noexcept-by-convention to satisfy std::lock_guard's
    // destructor contract.  pi_mutex_unlock can return EPERM if the
    // caller is not the owner; that's a programming error and is
    // swallowed here rather than thrown from a destructor frame.
    void unlock() noexcept
    {
        pi_mutex_unlock (&m_);
    }

    [[nodiscard]] bool try_lock() noexcept
    {
        return pi_mutex_trylock (&m_) == 0;
    }

    // Raw pi_mutex_t* for PiCond which must pass it through to
    // pi_cond_wait / pi_cond_signal.
    pi_mutex_t* native_handle() noexcept { return &m_; }

private:
    pi_mutex_t m_;
};

/* RAII wrapper around pi_cond_t.  Always paired with a PiMutex passed
 * to wait / signal / broadcast.
 *
 * Same non-copyable / non-movable rationale as PiMutex.
 *
 * Spurious wakes and pi_cond_wait's internal EAGAIN-retry path mean
 * every wait must sit inside a predicate loop (`while (! pred)
 * cv.wait (mtx);`).  This is the standard condvar contract; we don't
 * paper over it with a predicate overload because the loop is more
 * legible at the use site.
 */
class PiCond
{
public:
    PiCond() noexcept
    {
        pi_cond_init (&c_, 0);
    }

    explicit PiCond (PiShared /*tag*/) noexcept
    {
        pi_cond_init (&c_, RTPI_COND_PSHARED);
    }

    ~PiCond() noexcept
    {
        pi_cond_destroy (&c_);
    }

    PiCond (const PiCond&)            = delete;
    PiCond& operator= (const PiCond&) = delete;
    PiCond (PiCond&&)                 = delete;
    PiCond& operator= (PiCond&&)      = delete;

    // Caller must hold `mutex` on entry.  Releases it for the duration
    // of the wait and reacquires it before returning.  Spurious wakes
    // and EAGAIN-retries can occur — wrap in a predicate loop.
    void wait (PiMutex& mutex)
    {
        const int err = pi_cond_wait (&c_, mutex.native_handle());
        if (err != 0)
            throw std::system_error (err, std::system_category(), "pi_cond_wait");
    }

    // Timed variant of wait().  Same lock contract.  Returns true if
    // signalled, false on timeout.  Uses CLOCK_MONOTONIC (the default
    // clock for FUTEX_WAIT_REQUEUE_PI when RTPI_COND_CLOCK_REALTIME is
    // not set on the cond) — matches std::condition_variable::wait_for
    // and JUCE upstream WaitableEvent semantics, and is robust to
    // wall-clock jumps.  Spurious wakes can occur — wrap in a
    // predicate loop.
    template <class Rep, class Period>
    bool timed_wait (PiMutex& mutex, const std::chrono::duration<Rep, Period>& rel_time)
    {
        struct timespec abstime;
        clock_gettime (CLOCK_MONOTONIC, &abstime);

        const auto ns_total = std::chrono::duration_cast<std::chrono::nanoseconds> (rel_time).count();
        if (ns_total > 0)
        {
            abstime.tv_sec  += static_cast<time_t> (ns_total / 1'000'000'000LL);
            abstime.tv_nsec += static_cast<long>   (ns_total % 1'000'000'000LL);
            if (abstime.tv_nsec >= 1'000'000'000L)
            {
                abstime.tv_sec  += 1;
                abstime.tv_nsec -= 1'000'000'000L;
            }
        }

        const int err = pi_cond_timedwait (&c_, mutex.native_handle(), &abstime);
        if (err == 0)         return true;
        if (err == ETIMEDOUT) return false;
        throw std::system_error (err, std::system_category(), "pi_cond_timedwait");
    }

    // Wake exactly one waiter.  The woken thread is requeued onto the
    // mutex's PI chain so no inversion gap exists between wake and lock.
    void signal (PiMutex& mutex) noexcept
    {
        pi_cond_signal (&c_, mutex.native_handle());
    }

    void broadcast (PiMutex& mutex) noexcept
    {
        pi_cond_broadcast (&c_, mutex.native_handle());
    }

private:
    pi_cond_t c_;
};

// Placement-new helpers for shmem regions.  The shmem creator calls
// pi_mutex_init_shared / pi_cond_init_shared on the raw shmem memory;
// peer processes that mmap the same region get a fully usable
// PiMutex&/PiCond& via reinterpret_cast over the same bytes.
//
// Constructors are NOT used directly on shmem because pi_*_init is not
// idempotent across re-attaching processes — only the creator should
// initialise.

inline void pi_mutex_init_shared (pi_mutex_t* m) noexcept
{
    pi_mutex_init (m, RTPI_MUTEX_PSHARED);
}

inline void pi_cond_init_shared (pi_cond_t* c) noexcept
{
    pi_cond_init (c, RTPI_COND_PSHARED);
}
