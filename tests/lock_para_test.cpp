/* lock_para_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 24 Aug 2013
   FreeBSD-style copyright and disclaimer apply

   Tests for our various locks.

   Note about FairLocks: they perform poorly when there are more threads then
   processors. If misconfigured (threads > CPUs) then some of these tests can
   take forever to finish.

 */

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#define LOCKLESS_CHECK_ABORT 1

#include "lock.h"
#include "rwlock.h"
#include "seq_lock.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>
#include <array>

using namespace std;
using namespace lockless;


template<typename Lock>
void testMutex(const std::string& title)
{
    enum {
        Threads = 4,
        Iterations = 100000,
    };

    cerr << fmtTitle("Mutex - " + title) << endl;

    Lock lock;

    std::array<uint64_t, 10> values;
    values.fill(0);

    ParallelTest test;

    auto lockTh = [&] (unsigned id) {
        for (size_t it = 0; it < Iterations; ++it) {
            LockGuard<Lock> guard(lock);

            uint64_t value = values[0];
            for (size_t i = 0; i < values.size(); ++i) {
                locklessCheckEq(values[i], value, NullLog);
                values[i] = id;
            }
        }
    };
    test.add(lockTh, Threads);

    auto tryLockTh = [&] (unsigned id) {
        for (size_t it = 0; it < Iterations; ++it) {
            TryLockGuard<Lock> guard(lock);
            if (!guard) { it--; continue; }

            uint64_t value = values[0];
            for (size_t i = 0; i < values.size(); ++i) {
                locklessCheckEq(values[i], value, NullLog);
                values[i] = id;
            }
        }
    };
    test.add(tryLockTh, Threads);

    test.run();
}

BOOST_AUTO_TEST_CASE(mutexTest)
{
    testMutex<UnfairLock>("UnfairLock");
    testMutex<FairLock>("FairLock");
    testMutex<FairRWLock>("FairRWLock");
    testMutex< SeqLock<UnfairLock> >("SeqLock");
}


template<typename Lock>
void testRWLock(const std::string& title)
{
    enum {
        Threads = 4,
        Iterations = 10000,
    };

    cerr << fmtTitle("RWLock - " + title) << endl;

    Lock lock;
    std::array<uint64_t, 10> values;
    values.fill(0);

    auto readTh = [&] (unsigned) {
        for (size_t it = 0; it < Iterations; ++it) {
            ReadGuard<Lock> guard(lock);

            for (size_t i = 1; i < values.size(); ++i)
                locklessCheckEq(values[i], values[i-1], NullLog);
        }
    };

    auto writeTh = [&] (unsigned id) {
        for (size_t it = 0; it < Iterations; ++it) {
            LockGuard<Lock> guard(lock);

            uint64_t value = values[0];
            for (size_t i = 0; i < values.size(); ++i) {
                locklessCheckEq(values[i], value, NullLog);
                values[i] = id;
            }
        }
    };

    ParallelTest test;
    test.add(readTh, Threads);
    test.add(writeTh, Threads);
    test.run();
}

BOOST_AUTO_TEST_CASE(rwLockTest)
{
    testRWLock<FairRWLock>("FairRWLock");
}

BOOST_AUTO_TEST_CASE(seqLockTest)
{
    enum {
        Threads = 8,
        Iterations = 10000,
    };

    cerr << fmtTitle("SeqLock") << endl;

    typedef SeqLock<UnfairLock> Lock;
    Lock lock;
    std::array<size_t, 10> values;
    values.fill(0);

    auto readFn = [&] () {
        for (size_t i = 1; i < values.size(); ++i) {
            if (values[i] != values[i-1]) return false;
        }
        return true;
    };

    auto readTh = [&] (unsigned) {
        for (size_t it = 0; it < Iterations; ++it)
            locklessCheck(readRet<bool>(lock, readFn), NullLog);
    };

    auto writeTh = [&] (unsigned id) {
        for (size_t it = 0; it < Iterations; ++it) {
            LockGuard<Lock> guard(lock);

            uint64_t value = values[0];
            for (size_t i = 0; i < values.size(); ++i) {
                locklessCheckEq(values[i], value, NullLog);
                values[i] = id;
            }
        }
    };

    ParallelTest test;
    test.add(readTh, Threads);
    test.add(writeTh, Threads);
    test.run();
}
