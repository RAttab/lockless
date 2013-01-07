/* rcu.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 16 Dec 2012
   FreeBSD-style copyright and disclaimer apply

   Lightweight Read-Copy-Update implementation.
*/

#ifndef __lockless__rcu_h__
#define __lockless__rcu_h__

#include "debug.h"
#include "log.h"
#include "check.h"

#include <atomic>
#include <functional>
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
        doDeferred(epochs[0].deferList.load());
        doDeferred(epochs[1].deferList.load());
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
        size_t epoch = current.load();
        epochs[epoch % 2].count++;

        log.log(LogRcu, "enter", "epoch=%ld, count=%ld",
                epoch, epochs[epoch % 2].count.load());

        size_t oldOther = epoch - 1;
        if (!epochs[oldOther % 2].count.load()) {
            size_t oldCurrent = epoch;
            current.compare_exchange_weak(oldCurrent, oldCurrent + 1);
        }

        return epoch;
    }


    /* Blah

       Thread Safety: Completely lock-free and wait-free.

       Exception Safety: Does not throw.
     */
    void exit(size_t epoch)
    {
        auto& ep = epochs[epoch % 2];
        DeferEntry* deferHead = nullptr;


        size_t oldCount = ep.count.load();
        log.log(LogRcu, "exit", "epoch=%ld, count=%ld", epoch, oldCount);
        locklessCheckGt(oldCount, 0ULL, log);


        /* This avoids races with defer() because defer() only reads the list if
           it incremented the current epoch's count. So if we delete elements of
           the list when we're about to 0 the other epoch's count then it's
           impossible that the two op are working on the same epoch. Either
           because the count would be greater then 1 or because we'd be in
           current.

           Note that we can't decrement the counter before we write the head of
           the list because otherwise it could be swapped from under our nose.

           It's also possible for the counter to not go to zero after we write
           the head. This doesn't matter because we're in other and all new
           deferred work will be added to current. Since only other's deferred
           work can be executed, we'll have to wait until the counter reaches 0
           before any newer deferred work will be swaped to other and executed.

           It's also possible for other's counter to reach 0 without the head
           being written. In this case the deferred work will just be delayed
           until the next swap. No big deal as long as this doesn't happen too
           often.
        */
        if (oldCount == 1 && epoch != current.load()) {
            // I don't think this needs to be an atomic exchange.
            deferHead = ep.deferList.exchange(nullptr);

            log.log(LogRcu, "exit-defer", "epoch=%ld, head=%p",
                    epoch, deferHead);
        }

        ep.count--;

        if (deferHead) doDeferred(deferHead);
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
           with exit() by adding our entry in current's defer list.

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

        // It's safe to add our entry now.

        auto& head = epochs[oldCurrent % 2].deferList;
        DeferEntry* next;

        do {
            entry->next.store(next = head.load());
        } while (!head.compare_exchange_weak(next, entry));

        log.log(LogRcu, "add-defer",
                "epoch=%ld, head=%p, next=%p", oldCurrent, entry, next);

        // Exit the epoch which allows exit() to write the head again.
        exit(oldCurrent);
    }

    std::string print() const {
        size_t oldCurrent = current.load();
        size_t oldOther = oldCurrent - 1;

        std::array<char, 80> buffer;
        snprintf(buffer.data(), buffer.size(), "{ cur=%ld, count=[%ld, %ld] }",
                oldCurrent,
                epochs[oldCurrent % 2].count.load(),
                epochs[oldOther % 2].count.load());

        return std::string(buffer.data());
    }

private:

    struct DeferEntry
    {
        DeferEntry(const DeferFn& fn) : fn(fn) {}

        DeferFn fn;
        std::atomic<DeferEntry*> next;
    };

    void doDeferred(DeferEntry* entry)
    {
        log.log(LogRcu, "do-defer", "head=%p", entry);

        while (entry) {
            entry->fn();

            DeferEntry* next = entry->next;
            delete entry;
            entry = next;
        }
    }


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

    DebuggingLog<10240, DebugRcu>::type log;

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
