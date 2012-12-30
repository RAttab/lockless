/** rcu.h                                 -*- C++ -*-
    Rémi Attab (remi.attab@gmail.com), 16 Dec 2012
    FreeBSD-style copyright and disclaimer apply

    Lightweight Read-Copy-Update implementation.
*/

#ifndef __lockless__rcu_h__
#define __lockless__rcu_h__

#include <atomic>
#include <functional>
#include <cassert>
#include <cstddef>

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
    };

    struct Epoch
    {
        std::atomic<size_t> count;
        std::atomic<DeferEntry*> deferList;

        Epoch() : count(0), deferList(nullptr) {}
    };


    /* Blah

       Exception Safety: Only throws on calls to new.
     */
    Rcu() :
        current(new Epoch),
        other(new Epoch)
    {
#if 0
        assert(current.is_lock_free());

        Epoch* epoch = current.load();
        assert(epoch->count.is_lock_free());
        assert(epoch->deferList.is_lock_free());
#endif
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

        if (!oldOther->count) {
            doDeferred(oldOther);

            oldCurrent = current.load();
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


        /* Defered entries can only be executed and deleted when they are in
           other's list and other's counter is at 0. We can therfor avoid races
           with doDefer() by adding our entry in current's defer list.

           There's still the issue of current being swaped with other while
           we're doing our push. If this happens we can prevent the list from
           being deleted by incrementing current's counter and checking to see
           whether we're still in current after the counter was incremented.
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

        std::atomic<DeferEntry*>& head = oldCurrent->deferList;

        DeferEntry* next;
        do {
            entry->next.store(next = head.load());
        } while (!head.compare_exchange_weak(next, entry));


        // Exit the epoch which allows it to be swaped again.
        oldCurrent->count--;
    }

private:

    /** We can't add anything to the defered list unless count > 1.
        So if count == 0 then we can safely delete anything we want.
     */
    void doDeferred(Epoch* epoch)
    {
        DeferEntry* entry = epoch->deferList.exchange(nullptr);
        while (entry) {
            entry->fn();

            DeferEntry* next = entry->next;
            delete entry;
            entry = next;
        }
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
    RcuGuard(Rcu& rcu) :
        rcu(rcu),
        epoch(rcu.enter())
    {}

    ~RcuGuard() { rcu.exit(epoch); }

private:

    Rcu& rcu;
    Rcu::Epoch* epoch;

};

} // namespace lockless

#endif // __lockless__rcu_h__
