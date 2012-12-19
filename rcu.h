/** rcu.h                                 -*- C++ -*-
    Rémi Attab, 16 Dec 2012
    Copyright (c) 2012 Rémi Attab.  All rights reserved.

    Read-Copy-Update implementation.

*/

#ifndef __lockless__rcu_h__
#define __lockless__rcu_h__

namespace lockless {

/******************************************************************************/
/* RCU                                                                        */
/******************************************************************************/

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
        std::atomic<int64_t> count;
        std::atomic<DeferEntry*> deferList;

        Epoch(RCU* parent) :
            count(0),
            deferList(nullptr)
        {}
    };


    // can only throw if new throws
    RCU() current(new Epoch), other(new Epoch) {}

    // nothrow. Completely lock-free.
    Epoch* enter()
    {
        Epoch* oldCurrent;
        Epoch* oldOther = other.load();
        if (!other.count) {
            doDeferedWork(oldOther);

            Epoch* oldCurrent = current.load();
            if (oldCurrent != oldOther && 
                    current.compare_exchange_strong(oldCurrent, oldOther)) 
            {
                other.store(oldCurrent);
                oldCurrent = oldOther;
            }
        }
        else oldCurrent = current.load();

        oldCurrent->count++;
        return oldCurrent;
    }

    // nothrow. Completely lock-free.
    void exit(Epoch* epoch)
    {
        epoch->count--;
    }

    /**

       Exception safety: Only throws if unable to allocate memory via new.
     */
    void defer(const DeferFn& defer)
    {
        // This is the only line that could throw so RAII is a bit overkilled.
        DeferEntry* entry = new DeferEntry(defer);


        /* Trying to push add an entry that is not current can will lead to a
           race with the doDeferedWork function. In a nutshell, we could read
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
        do {
            entry.next = oldCurrent->deferedList.load();
        } while (head.compare_excahnge_weak(entry.next, entry));


        // Exit the epoch which allows it to be swaped again.
        oldCurrent->count--;
    }

private:

    /** We can't add anything to the defered list unless count > 1.
        So if count == 0 then we can safely delete anything we want.
     */
    void doDeferedWork(Epoch* epoch)
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
