/* rwlock.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 24 Aug 2013
   FreeBSD-style copyright and disclaimer apply

   Read-Write locks
*/

#ifndef __lockless__rwlock_h__
#define __lockless__rwlock_h__

#include "lock.h"
#include "utils.h"

#include <atomic>


namespace lockless {

/******************************************************************************/
/* FAIR RW LOCK                                                               */
/******************************************************************************/

/** Elegant extension of the ticket lock for RWLock (I didn't invent this).
 */
struct FairRWLock : public Lock
{
    locklessEnum uint64_t Mask = 0xFFFF;

    FairRWLock() { d.all = 0; }

    void lock()
    {
        uint16_t ticket = d.split.tickets.fetch_add(1);
        while (ticket != d.split.writes);
    }

    bool tryLock()
    {
        uint64_t val;
        uint64_t old = d.all;

        do {
            uint16_t writes = old >> 16;
            uint16_t tickets = old >> 32;
            if (writes != tickets) return false;

            tickets++;
            val = (old & ~(Mask << 32)) | (uint64_t(tickets) << 32);

        } while (!d.all.compare_exchange_weak(old, val));

        return true;
    }

    void unlock()
    {
        /* This function is implemented this way to avoid an atomic increment
           and go for a simple load store operation instead.
         */

        uint16_t reads = uint16_t(d.split.reads) + 1;
        uint16_t writes = uint16_t(d.split.writes) + 1;

        // Note that tickets can still be modified so we can't update d.all.
        d.rw = uint32_t(reads) | uint32_t(writes) << 16;
    }

    void readLock()
    {
        uint16_t ticket = d.split.tickets.fetch_add(1);
        while (ticket != d.split.reads);

        // Since we've entered a read section, allow other reads to continue.
        d.split.reads++;
    }

    bool tryReadLock()
    {
        uint64_t val;
        uint64_t old = d.all;

        do {
            uint16_t reads = old;
            uint16_t tickets = old >> 32;

            if (reads != tickets) return false;

            reads++;
            tickets++;
            val = (old & (Mask << 16)) | uint64_t(tickets) << 32 | reads;
        } while (!d.all.compare_exchange_weak(old, val));

        return true;
    }

    void readUnlock()
    {
        d.split.writes++;
    }

private:

    union {
        struct {
            std::atomic<uint16_t> reads;
            std::atomic<uint16_t> writes;
            std::atomic<uint16_t> tickets;
        } split;
        std::atomic<uint32_t> rw;
        std::atomic<uint64_t> all;
    } d;
};

/******************************************************************************/
/* READ GUARD                                                                 */
/******************************************************************************/

template<typename Lock>
struct ReadGuard
{
    ReadGuard(Lock& lock) : pLock(&lock)
    {
        pLock->readLock();
    }

    ~ReadGuard()
    {
        release();
    }

    void release()
    {
        if (!pLock) return;
        pLock->readUnlock();
        pLock = nullptr;
    }

private:
    Lock* pLock;
};


/******************************************************************************/
/* TRY READ GUARD                                                             */
/******************************************************************************/

template<typename Lock>
struct TryReadGuard
{
    TryReadGuard(Lock& lock) :
        pLock(&lock),
        locked(pLock->tryReadLock())
    {}

    ~TryReadGuard()
    {
        release();
    }

    void release()
    {
        if (!pLock) return;
        if (locked) pLock->readUnlock();
        pLock = nullptr;
        locked = false;
    }

    operator bool() const { return locked; }

private:
    Lock* pLock;
    bool locked;
};

} // lockless

#endif // __lockless__rwlock_h__
