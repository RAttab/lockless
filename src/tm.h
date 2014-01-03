/* time.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 16 May 2013
   FreeBSD-style copyright and disclaimer apply

   Time related utilities.

   \todo Dig in cycle counter a little bit more.
*/

#ifndef __lockless__time_h__
#define __lockless__time_h__

#include "arch.h"

#include <chrono>
#include <thread>
#include <cstdlib>
#include <time.h>
#include <unistd.h>

namespace lockless {


/******************************************************************************/
/* SLEEP                                                                      */
/******************************************************************************/

inline void sleep(size_t ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

/******************************************************************************/
/* WALL                                                                       */
/******************************************************************************/

/** Plain old boring time taken from the kernel using a syscall. */
struct Wall
{
    typedef double ClockT;
    enum { CanWrap = false };

    ClockT operator() () const locklessAlwaysInline
    {
        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
            return -1;

        return ts.tv_sec + (ts.tv_nsec * 0.0000000001);
    }

    static constexpr double toSec(ClockT t) { return t; }
    static constexpr ClockT diff(ClockT first, ClockT second)
    {
        return second - first;
    }

};

inline Wall::ClockT wall() { return Wall()(); }


/******************************************************************************/
/* RDTSC                                                                      */
/******************************************************************************/

/** Samples the time (not cycles) register for the current CPU. Does not account
    for CPU migration of the current thread or for the power-state of the CPU.
    Both of these issues can lead to skewed results.

    To figure out whether this represent time or cycles, check for the
    constant_tsc flag in /proc/cpuinfo.

    \todo CPU cycles only seem to be available through performance counters
    which is CPU specific and require a kernel driver to enable in userland.
    Bummer.
 */
struct Rdtsc
{
    typedef uint64_t ClockT;
    enum { CanWrap = false };

    ClockT operator() () const locklessAlwaysInline
    {
        unsigned high, low;
        asm volatile ("rdtsc" : "=a" (high), "=d" (low));
        return uint64_t(high) << 32 | low;
    }

    // \todo toSec implementation.

    static constexpr ClockT diff(ClockT first, ClockT second)
    {
        return first > second ? ~ClockT(0) - first + second : second - first;
    }

};

inline Rdtsc::ClockT rdtsc() { return Rdtsc()(); }


/** Same as rdtsc except that it introduces a load fence before the counter is
    sampled.

    \todo Need to experiment to figure out when to use which in which
    situations.
 */
struct Rdtscp
{
    typedef uint64_t ClockT;
    enum { CanWrap = false };

    ClockT operator() () const locklessAlwaysInline
    {
        unsigned high, low;
        asm volatile ("rdtscp" : "=a" (high), "=d" (low));
        return uint64_t(high) << 32 | low;
    }

    // \todo toSec implementation.

    static constexpr ClockT diff(ClockT first, ClockT second)
    {
        return first > second ? ~ClockT(0) - first + second : second - first;
    }

};


inline Rdtscp::ClockT rdtscp() { return Rdtscp()(); }


/******************************************************************************/
/* MONOTONIC                                                                  */
/******************************************************************************/

/** Monotonic uses rdtsc except that it also protects us from the process/thread
    migrating to other CPU.

    It doesn't take into account the various power-state of the CPU which could
    skew the results. The only way to account for this is to count cycles or to
    structure the test such that there's enough warmup to get the CPU to wake
    up. Unfortunately, warmups are not always feasable and accessing performance
    counters requires (I think) a kernel module to enable them in userland.

    Sadly, we live in an imperfect world.
 */
struct Monotonic
{
    typedef double ClockT;
    enum { CanWrap = false };

    ClockT operator() () const locklessAlwaysInline
    {
        struct timespec ts;
        if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) < 0)
            return -1;

        return ts.tv_sec + (ts.tv_nsec * 0.0000000001);
    }

    static constexpr double toSec(ClockT t) { return t; }
    static constexpr ClockT diff(ClockT first, ClockT second)
    {
        return second - first;
    }

};

inline Monotonic::ClockT monotonic() { return Monotonic()(); }


/** Same as monotonic except that it returns only the nsec portion of the sample
    and will therefor wrap every second. Used to avoid floating-point ops in
    latency sensitive measurements.
 */
struct NsecMonotonic
{
    typedef uint64_t ClockT;
    enum { CanWrap = true };

    ClockT operator() () const locklessAlwaysInline
    {
        struct timespec ts;
        if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) < 0)
            return -1;

        return ts.tv_nsec;
    }

    static constexpr double toSec(ClockT t)
    {
        return double(t) * 0.0000000001;
    }

    static constexpr ClockT diff(ClockT first, ClockT second)
    {
        return first > second ? 1000000000ULL - first + second : second - first;
    }

};

inline NsecMonotonic::ClockT nsecMonotonic() { return NsecMonotonic()(); }


/******************************************************************************/
/* TIMER                                                                      */
/******************************************************************************/

template<typename Clock>
struct Timer
{
    typedef typename Clock::ClockT ClockT;

    Timer() : start(clock()) {}

    double elapsed() const locklessAlwaysInline
    {
        return Clock::diff(start, clock());
    }

    double reset() locklessAlwaysInline
    {
        ClockT end = clock();
        ClockT elapsed = Clock::diff(start, end);
        start = end;
        return Clock::toSec(elapsed);
    }

private:
    Clock clock;
    ClockT start;
};

} // lockless

#endif // __lockless__time_h__
