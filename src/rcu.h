/* rcu.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 16 Dec 2012
   FreeBSD-style copyright and disclaimer apply

   Lightweight Read-Copy-Update implementation.
*/

#ifndef __lockless__rcu_h__
#define __lockless__rcu_h__

#include "debug.h"
#include "log.h"

#include <atomic>
#include <functional>
#include <cassert>
#include <cstddef>

namespace lockless {

/******************************************************************************/
/* RCU                                                                        */
/******************************************************************************/

/* Blah

 */
struct Rcu
{
    typedef std::function<void()> DeferFn;


    /* Blah

       Exception Safety: Only throws on calls to new.
     */
    Rcu() : current(0)
    {
        epochs[0].init();
        epochs[1].init();
    }


    /* Blah.

       Exception Safety: Does not throw.
     */
    ~Rcu()
    {
        doDeferred(0);
        doDeferred(1);
    }

    Rcu(const Rcu&) = delete;
    Rcu& operator=(const Rcu&) = delete;


    /* Blah

       Thread Safety: Can issue calls to delete which could lock. Everything
           else is lock-free and wait-free.

       Exception safety: Does not throw.
     */
    size_t enter()
    {
        size_t oldCurrent = current.load();
        size_t oldOther = oldCurrent + 1;

        log.log(LogRcu, "ENTER", "epoch=%ld, oldCount=%ld",
                oldCurrent, epochs[oldOther % 2].count.load());

        if (!epochs[oldOther % 2].count) {
            doDeferred(oldOther);
            if (current.compare_exchange_strong(oldCurrent, oldCurrent + 1))
                oldCurrent++;

            log.log(LogRcu, "SWAP", "epoch=%ld", oldCurrent);
        }

        epochs[oldCurrent % 2].count++;
        return oldCurrent;
    }


    /* Blah

       Thread Safety: Completely lock-free and wait-free.

       Exception Safety: Does not throw.
     */
    void exit(size_t current)
    {
        log.log(LogRcu, "EXIT", "epoch=%ld, count=%ld",
                current, epochs[current % 2].count.load());

        assert(epochs[current % 2].count > 0);
        epochs[current % 2].count--;
    }


    /* Blah

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
        size_t oldCurrent;
        while (true) {
            oldCurrent = current.load();
            epochs[oldCurrent % 2].count++;
            if (current.load() == oldCurrent) break;

            // Not safe to insert the entry, try again.
            epochs[oldCurrent % 2].count--;
            continue;
        }

        auto& head = epochs[oldCurrent % 2].deferList;
        DeferEntry* next;

        do {
            entry->next.store(next = head.load());
        } while (!head.compare_exchange_weak(next, entry));

        log.log(LogRcu, "DEFER",
                "epoch=%ld, head=%p, next=%p", oldCurrent, entry, next);

        // Exit the epoch which allows it to be swaped again.
        epochs[oldCurrent % 2].count--;
    }

private:

    /* We can't add anything to the defered list unless count > 1.
       So if count == 0 then we can safely delete anything we want.
     */
    void doDeferred(size_t epoch)
    {
        DeferEntry* entry = epochs[epoch % 2].deferList.exchange(nullptr);

        log.log(LogRcu, "DO_DEFER", "epoch=%ld, head=%p", epoch, entry);

        while (entry) {
            entry->fn();

            DeferEntry* next = entry->next;
            delete entry;
            entry = next;
        }
    }


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

        void init()
        {
            count.store(0);
            deferList.store(nullptr);
        }
    };


    std::atomic<size_t> current;
    Epoch epochs[2];

public:

    DebuggingLog<1024, DebugRcu>::type log;

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
    size_t epoch;

};

} // namespace lockless

#endif // __lockless__rcu_h__
