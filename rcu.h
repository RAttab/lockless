/** rcu.h                                 -*- C++ -*-
    Rémi Attab, 16 Dec 2012
    Copyright (c) 2012 Rémi Attab.  All rights reserved.

    Read-Copy-Update implementation.

*/

#ifndef __lockless__rcu_h__
#define __lockless__rcu_h__

#include <cassert>
#include <atomic>

namespace lockless {

/******************************************************************************/
/* RCU                                                                        */
/******************************************************************************/

/* Blah

   \todo Hide the implementation and the Epoch & DeferEntry classes in a cpp
       file. Need a build system first though.
 */
struct Rcu
{
    typedef std::function<void()> DeferFn;

    struct DeferEntry
    {
        DeferEntry(const DeferFn& fn) : fn(fn) {}

        DeferFn fn;
        std::atomic<DeferEntry*> next;
    }

    struct Epoch
    {
        std::atomic<uint64_t> count;
        std::atomic<DeferEntry*> deferList;

        Epoch(RCU* parent) :
            count(0),
            deferList(nullptr)
        {}
    };


    /* Blah

       Exception Safety: Only throws on calls to new.
     */
    Rcu() :
        current(new Epoch),
        other(new Epoch)
    {
        assert(current.is_lock_free());

        Epoch* epoch = current.load();
        assert(epoch->count.is_lock_free());
        assert(epoch->deferList.is_lock_free());
    }

    /* Blah.

       Exception Safety: Does not throw.
     */
    ~Rcu()
    {
        doDeferred(current.load());
        doDeferred(other.load());
    }

    /* Blah

       Thread Safety: Can issue calls to delete which could lock. Everything
           else is lock-free and wait-free.

       Exception safety: Does not throw.
     */
    Epoch* enter()
    {
        Epoch* oldCurrent;
        Epoch* oldOther = other.load();
        if (!other.count) {
            doDeferred(oldOther);

            Epoch* oldCurrent = current.load();
            if (oldCurrent != oldOther &&
                    current.compare_exchange_strong(oldCurrent, oldOther))
            {
                other.store(oldCurrent);
            }
        }
        else oldCurrent = current.load();

        oldCurrent->count++;
        return oldCurrent;
    }

    /* Blah

       Thread Safety: Completely lock-free and wait-free.

       Exception Safety: Does not throw.
     */
    void exit(Epoch* epoch)
    {
        assert(epoch->count > 0);
        epoch->count--;
    }

    /** Blah

        Thread safety: Issues a single call to new which could lock. Everything
           else is lock-free and wait-free.

        Exception safety: Issues a single call to new which may throw.
            Everything else is nothrow.
     */
    void defer(const DeferFn& defer)
    {
        // This is the only line that could throw so RAII is a bit overkilled.
        DeferEntry* entry = new DeferEntry(defer);


        /* Trying to push add an entry that is not current can will lead to a
           race with the doDeferred function. In a nutshell, we could read
           entries in the list that have been deleted.

           This is prevented by incrementing the epoch's count on current which
           prevents current from being swaped to other. So all in all we're
           safe.
        */
        Epoch* oldCurrent;
        while (true) {
            oldCurrent = current.load();
            oldCurrent->count++;
            if (current.load() == oldCurrent) break;

            // Not safe to insert the entry, try again.
            oldCurrent->count--;
            continue;
        }

        EntryList& head = oldCurrent->deferedList;
        entry.next = oldCurrent->deferedList.load();
        while (!head.compare_exchange_weak(entry.next, entry));


        // Exit the epoch which allows it to be swaped again.
        oldCurrent->count--;
    }

private:

    /** We can't add anything to the defered list unless count > 1.
        So if count == 0 then we can safely delete anything we want.
     */
    void doDeferred(Epoch* epoch)
    {
        assert(epoch.count == 0);

        DeferEntry* entry = epoch.deferList.exchange(nullptr);
        while (entry) {
            entry->fn();

            DeferEntry* next = entry->next;
            delete entry;
            entry = next;
        }

        assert(epoch.count == 0);
    }

    std::atomic<Epoch*> current;
    std::atomic<Epoch*> other;
};



/******************************************************************************/
/* RCU GUARD                                                                  */
/******************************************************************************/

/* Blah

 */
struct RcuGuard
{
    RcuGuard(const Rcu& rcu) :
        rcu(rcu),
        epoch(rcu.enter())
    {}

    ~RcuGuard() { rcu.exit(epoch); }

private:

    const Rcu& rcu;
    Rcu::Epoch* epoch;

};

} // namespace lockless

#endif // __lockless__rcu_h__
