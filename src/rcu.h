/* rcu.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 16 Dec 2012
   FreeBSD-style copyright and disclaimer apply

   Lightweight Read-Copy-Update implementation.
*/

#ifndef __lockless__rcu_h__
#define __lockless__rcu_h__

#include "rcu_guard.h"
#include "lock.h"
#include "list.h"
#include "debug.h"
#include "log.h"
#include "check.h"
#include "arch.h"

#include <atomic>
#include <functional>
#include <cstddef>

namespace lockless {

/******************************************************************************/
/* RCU                                                                        */
/******************************************************************************/

struct Rcu : public Lock
{
    typedef std::function<void()> DeferFn;

    Rcu() : current(0)
    {
        for (size_t i = 0; i < 2; ++i)
            epochs[0].init();
    }

    ~Rcu()
    {
        for (size_t i = 0; i < 2; ++i)
            doDeferred(epochs[i].deferList.head.exchange(nullptr));
    }

    size_t enter()
    {
        size_t epoch;

        /** The extra loop is to guard against the following race:

            1) Thread A reads current E and gets preempted.
            2) Thread B enters, incs E+1, moves epoch forward and exits.
            3) Thread C enters, incs E+1, and reads E's count (0).
            4) Thread A wakes up, incs count and exits.
            5) Thread C moves epoch forward and exits.
            6) Thread B and C vacates the epoch and trigger a gc.

            This is an issue because thread A entered its epoch before C but
            it's not taken into account when defering epoch E+1. This breaks the
            RCU guarantees which is bad(tm).

            Since this is the same fundamental race as GlobalRcu, we'll use the
            same solution: don't exit while entering an epoch that isn't
            (equivalent to) current. Note that so long as we don't exit the
            function, we haven't incured any risk vis-a-vis the user reading
            data that it shouldn't read so its fine if we back off and try
            again.
         */
        while (true) {
            epoch = current;
            epochs[epoch & 1].count++;

            if ((epoch & 1) == (current & 1)) break;
            epochs[epoch & 1].count--;
        }

        log(LogRcu, "enter", "epoch=%ld, count=%ld",
                epoch, epochs[epoch & 1].count);

        size_t oldOther = epoch - 1;
        if (!epochs[oldOther & 1].count) {
            size_t oldCurrent = epoch;
            current.compare_exchange_weak(oldCurrent, oldCurrent + 1);
        }

        return epoch;
    }


    void exit(size_t epoch)
    {
        auto& ep = epochs[epoch & 1];

        size_t oldCount = ep.count;
        log(LogRcu, "exit", "epoch=%ld, count=%ld", epoch, oldCount);
        locklessCheckGt(oldCount, 0ULL, log);


        /* Note that we can't execute any defered work if we're in current
           because other may not have been fully vacated yet which means that
           the read-side critical section is still live.

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
        ListNode<DeferFn>* deferHead = nullptr;
        if (oldCount == 1 && epoch != current) {
            // I don't think this needs to be atomic.
            deferHead = ep.deferList.head.exchange(nullptr);

            log(LogRcu, "exit-defer", "epoch=%ld, head=%p",
                    epoch, deferHead);
        }

        ep.count--;

        if (deferHead) doDeferred(deferHead);
    }

    template<typename Fn>
    void defer(Fn&& defer)
    {
        // This is the only line that could throw so RAII is a bit overkilled.
        auto* node = new ListNode<DeferFn>(std::forward<Fn>(defer));

        /* Any delay between the epoch read and the push to defer list won't
           matter because, at worst, the defered entry will be executed later
           then it should which doesn't impact anything.
         */
        size_t epoch = current;
        epochs[epoch & 1].deferList.push(node);

        log(LogRcu, "add-defer", "epoch=%ld, head=%p", epoch, node);
    }

    std::string print() const
    {
        size_t oldCurrent = current;
        size_t oldOther = oldCurrent - 1;

        return format("{ cur=%ld, count=[%ld, %ld] }",
                oldCurrent,
                epochs[oldCurrent & 1].count,
                epochs[oldOther & 1].count);
    }

private:

    void doDeferred(ListNode<DeferFn>* node)
    {
        log(LogRcu, "do-defer", "head=%p", node);

        while (node) {
            node->value();

            auto* next = node->next();
            delete node;
            node = next;
        }
    }


    struct locklessCacheAligned Epoch
    {
        std::atomic<size_t> count;
        List<DeferFn> deferList;

        void init()
        {
            count.store(0);
            deferList.head = nullptr;
        }
    };


    locklessCacheAligned std::atomic<size_t> current;
    Epoch epochs[2];

public:
    DebuggingLog<10240, DebugRcu>::type log;
};

} // namespace lockless

#endif // __lockless__rcu_h__
