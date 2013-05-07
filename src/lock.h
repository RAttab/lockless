/* lock.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 12 Feb 2013
   FreeBSD-style copyright and disclaimer apply

   REALLY simple spin lock useful for debugging code.

   I'm also aware of how ironic this is...

   \todo Make this an actual robust spin lock.
*/

#ifndef __lockless__lock_h__
#define __lockless__lock_h__

#include <cstddef>
#include <atomic>

namespace lockless {


/******************************************************************************/
/* LOCK                                                                       */
/******************************************************************************/

struct Lock
{
    Lock() : val(0) {}

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
