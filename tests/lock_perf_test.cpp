/* lock_perf_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 31 Aug 2013
   FreeBSD-style copyright and disclaimer apply

   Performance tests to compare the various lock types.

   \todo I don't think the current approach is the right way to perf a mutex.
*/

#include "lock.h"
#include "perf_utils.h"

using namespace std;
using namespace lockless;

/******************************************************************************/
/* MUTEX                                                                      */
/******************************************************************************/

template<typename Lock>
size_t doSimpleMutexThread(Lock& lock, unsigned)
{
    enum { Iterations = 100 };

    for (size_t it = 0; it < Iterations; ++it)
        LockGuard<Lock> guard(lock);

    return Iterations;
}

template<typename Lock>
void runSimpleMutexTest(
        const std::string& name, unsigned thCount, unsigned lengthMs)
{
    PerfTest<Lock> perf;
    perf.add(name, doSimpleMutexThread<Lock>, thCount);
    perf.run(lengthMs);

    cerr << perf.printStats(name) << endl;
}


/******************************************************************************/
/* MAIN                                                                       */
/******************************************************************************/

int main(int argc, char** argv)
{
    unsigned thCount = 8;
    if (argc > 1) thCount = stoul(string(argv[1]));

    size_t lengthMs = 1000;
    if (argc > 2) lengthMs = stoull(string(argv[2]));

    runSimpleMutexTest<UnfairLock>("unfair", thCount, lengthMs);
    runSimpleMutexTest<FairLock>("fair", thCount, lengthMs);
}
