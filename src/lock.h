/* lock.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 12 Feb 2013
   FreeBSD-style copyright and disclaimer apply

   Collection of toy spin locks.

   I'm aware of how ironic this is...
*/

#ifndef __lockless__lock_h__
#define __lockless__lock_h__

#include "utils.h"

#include <cstddef>
#include <atomic>

namespace lockless {


/******************************************************************************/
/* LOCK                                                                       */
/******************************************************************************/

// Simple tag for lock-like structures which can't be moved or copied.
struct Lock
{
    Lock() {}
    Lock(Lock&&) = delete;
    Lock(const Lock&) = delete;
    Lock& operator=(Lock&&) = delete;
    Lock& operator=(const Lock&) = delete;
};


/******************************************************************************/
/* UNFAIR LOCK                                                                */
/******************************************************************************/

struct UnfairLock : public Lock
{
    UnfairLock() : val(0) {}

    void lock()
    {
        size_t oldVal;
        while((oldVal = val) || !val.compare_exchange_weak(oldVal, 1));
    }

    bool tryLock()
    {
        size_t oldVal = val;
        return !oldVal && val.compare_exchange_strong(oldVal, 1);
    }

    void unlock() { val.store(0); }

private:
    std::atomic<size_t> val;
};


/******************************************************************************/
/* FAIR LOCK                                                                  */
/******************************************************************************/

/**
   The reason why we use atomic functions instead of AtomicPod or std::atomic is
   because tryLock needs to make a copy of our array

 */
struct FairLock : public Lock
{
    FairLock()
    {
        d.packed = 0;
    }

    void lock()
    {
        uint32_t tickets = d.split.tickets.fetch_add(1);
        while (tickets != d.split.serving);
    }

    bool tryLock()
    {
        uint64_t val;
        uint64_t old = d.packed;

        do {
            // The elaborate setup is to gracefully wrap on overflow.
            uint32_t tickets = old >> 32;
            uint32_t serving = old;

            if (tickets != serving) return false;

            tickets++;
            val = (uint64_t(tickets) << 32) | serving;

        } while (!d.packed.compare_exchange_weak(old, val));

        return true;
    }

    void unlock()
    {
        // we have the exclusive lock so we're the only one that can modify
        // serving which means that we don't need the atomic inc.
        d.split.serving = d.split.serving + 1;
    }

private:

    /** This struct allows us to work on both the split values and the whole in
        an atomic fashion while also gracefully handling overflows.
     */
    union
    {
        struct
        {
            std::atomic<uint32_t> serving;
            std::atomic<uint32_t> tickets;
        } split;

        std::atomic<uint64_t> packed;

    } d;
};

/******************************************************************************/
/* LOCK GUARD                                                                 */
/******************************************************************************/

template<typename Lock>
struct LockGuard
{
    LockGuard(Lock& lock) : pLock(&lock)
    {
        pLock->lock();
    }

    ~LockGuard()
    {
        release();
    }

    void release()
    {
        if (!pLock) return;
        pLock->unlock();
        pLock = nullptr;
    }

private:
    Lock* pLock;
};


/******************************************************************************/
/* TRY LOCK GUARD                                                             */
/******************************************************************************/


template<typename Lock>
struct TryLockGuard
{
    TryLockGuard(Lock& lock) :
        pLock(&lock),
        locked(pLock->tryLock())
    {}

    ~TryLockGuard()
    {
        release();
    }

    void release()
    {
        if (!pLock) return;
        if (locked) pLock->unlock();
        pLock = nullptr;
        locked = false;
    }

    operator bool() const { return locked; }

private:
    Lock* pLock;
    bool locked;
};

} // lockless

#endif // __lockless__lock_h__
