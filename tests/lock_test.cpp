/* lock_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 31 Aug 2013
   FreeBSD-style copyright and disclaimer apply

   Tests for the various lock implementations.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#define LOCKLESS_CHECK_ABORT 1

#include <iostream>

#include "lock.h"
#include "rwlock.h"
#include "seq_lock.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>

using namespace std;
using namespace lockless;


template<typename Lock>
void testMutex(const std::string& title)
{
    cerr << fmtTitle("Mutex - " + title) << endl;

    Lock lock;

    for (size_t it = 0; it < 10; ++it) {
        lock.lock();
        locklessCheck(!lock.tryLock(), NullLog);
        lock.unlock();
        locklessCheck(lock.tryLock(), NullLog);
        lock.unlock();

        {
            LockGuard<Lock> guard(lock);

            TryLockGuard<Lock> tryGuard(lock);
            locklessCheck(!tryGuard, NullLog);
        }

        {
            TryLockGuard<Lock> tryGuard(lock);
            locklessCheck(tryGuard, NullLog);
        }
    }
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
    cerr << fmtTitle("RWLock - " + title) << endl;

    Lock lock;

    for (size_t it = 0; it < 10; ++it) {

        for (size_t j = 0; j < 5; ++j) lock.readLock();
        for (size_t j = 0; j < 5; ++j)
            locklessCheck(lock.tryReadLock(), NullLog);

        locklessCheck(!lock.tryLock(), NullLog);

        for (size_t j = 0; j < 10; ++j) lock.readUnlock();

        lock.lock();
        locklessCheck(!lock.tryReadLock(), NullLog);
        lock.unlock();
        lock.readLock();
        lock.readUnlock();

        {
            ReadGuard<Lock> g0(lock);
            ReadGuard<Lock> g1(lock);
            locklessCheck(!lock.tryLock(), NullLog);
        }

        {
            LockGuard<Lock> wguard(lock);
            TryReadGuard<Lock> rguard(lock);
            locklessCheck(!rguard, NullLog);
        }
    }
}

BOOST_AUTO_TEST_CASE(rwLockTest)
{
    testRWLock<FairRWLock>("FairRWLock");
}

BOOST_AUTO_TEST_CASE(seqLockTest)
{
    cerr << fmtTitle("SeqLock") << endl;

    typedef SeqLock<UnfairLock> Lock;
    Lock lock;

    for (size_t it = 0; it < 10; ++it) {
        locklessCheck(lock.commit(lock.begin()), NullLog);

        {
            size_t v = lock.begin();
            lock.lock();
            locklessCheck(!lock.commit(v), NullLog);
            lock.unlock();
        }

        // Can't test much else because everything else ends up in a deadlock.
    }
}
