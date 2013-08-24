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
        return !oldVal && val.compare_exchange_weak(oldVal, 1);
    }

    void unlock() { val.store(0); }

private:
    std::atomic<size_t> val;
};


/******************************************************************************/
/* FAIR LOCK                                                                  */
/******************************************************************************/

struct FairLock : public Lock
{
    typedef uint32_t Word;
    locklessEnum unsigned ServingShift = 0;
    locklessEnum unsigned TicketsShift = 16;

    locklessEnum Word OneServing = Word(1) << ServingShift;
    locklessEnum Word OneTicket = Word(1) << TicketsShift;

    FairLock() : data(0) {}

    void lock()
    {
        Word value = data.fetch_add(OneTicket);
        Word ticket = tickets(value);
        while (ticket != serving(data));
    }

    bool tryLock()
    {
        Word old = data;

        do {
            if (serving(old) != tickets(old)) return false;
        } while (!data.compare_exchange_weak(old, old + OneTicket));

        return true;
    }

    void unlock()
    {
        data += OneServing;
    }

private:

    Word serving(Word d) const { return (d >> ServingShift) & Word(0xFFFF); }
    Word tickets(Word d) const { return (d >> TicketsShift) & Word(0xFFFF); }

    std::atomic<Word> data;
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
        pLock->unlock();
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
