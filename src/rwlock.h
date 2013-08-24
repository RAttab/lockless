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
    typedef uint32_t Word;

    locklessEnum unsigned ReadsShift   = 0;
    locklessEnum unsigned WritesShift  = 8;
    locklessEnum unsigned TicketsShift = 16;

    locklessEnum Word OneRead   = Word(1) << ReadsShift;
    locklessEnum Word OneWrite  = Word(1) << WritesShift;
    locklessEnum Word OneTicket = Word(1) << TicketsShift;


    FairRWLock() : data (0) {}

    void lock()
    {
        Word value = data.fetch_add(OneTicket);
        Word ticket = tickets(value);
        while (ticket != writes(data));
    }

    bool tryLock()
    {
        Word old = data;

        do {
            if (writes(old) != tickets(old)) return false;
        } while (!data.compare_exchange_weak(old, old + OneTicket));

        return true;
    }

    void unlock()
    {
        // \todo Not sure if atomic inc is faster then load/store.
        data = data + OneReads + OneWrites;
    }

    void readLock()
    {
        auto value = data.fetch_add(OneTicket);
        auto ticket = tickets(value);
        while (ticket != reads(data));

        // Unlock the next read to go through.
        data += OneRead;
    }

    void tryReadLock()
    {
        Word old = data;

        do {
            if (reads(old) != tickets(old)) return false;
        } while (!data.compare_exchange_weak(old, old + OneRead + OneTicket));

        return true;
    }

    void readUnlock()
    {
        data += OneWrite;
    }

private:
    Word reads(Word d) const { return (d >> ReadsShift) & Word(0xFF); }
    Word writes(Word d) const { return (d >> WritesShift) & Word(0xFF); }
    Word tickets(Word d) const { return (d >> TicketsShift) & Word(0xFF); }

    std::atomic<Word> data;
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

#endif // __lockless__rwlock_h__
