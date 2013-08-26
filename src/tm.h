/* time.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 16 May 2013
   FreeBSD-style copyright and disclaimer apply

   Time related utilities.

   \todo Dig in cycle counter a little bit more.
*/

#ifndef __lockless__time_h__
#define __lockless__time_h__

#include <chrono>
#include <thread>
#include <cstdlib>
#include <time.h>
#include <unistd.h>

namespace lockless {


/******************************************************************************/
/* SLEEP                                                                      */
/******************************************************************************/

void sleep(size_t ms)
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
} wall;


/******************************************************************************/
/* RDTSC                                                                      */
/******************************************************************************/

/** Samples the time (not cycles) register for the current CPU. Does not account
    for CPU migration of the current thread or for the power-state of the CPU.
    Both of these issues can lead to skewed results.

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
} rdtsc;


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
} rdtscp;


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
} monotonic;


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
} nsecMonotonic;


/******************************************************************************/
/* TIMER                                                                      */
/******************************************************************************/

template<typename Clock>
struct Timer
{
    typedef typename Clock::ClockT ClockT;

    Timer()
    {
        adj = clock();
        start = clock();
        adj = diff(adj, start);
    }

    double elapsed() const locklessAlwaysInline
    {
        return diff(start, clock()) - adj;
    }

    double reset() locklessAlwaysInline
    {
        ClockT end = clock();
        ClockT elapsed = diff(start, end) - adj;
        start = end;
        return elapsed;
    }

private:

    uint64_t diff(uint64_t first, uint64_t second) const locklessAlwaysInline
    {
        if (Clock::CanWrap && second < first)
            return ~uint64_t(0) - second + first;
        return second - first;
    }

    Clock clock;
    ClockT start;
    ClockT adj;
};

} // lockless

#endif // __lockless__time_h__
