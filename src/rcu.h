/* rcu.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 16 Dec 2012
   FreeBSD-style copyright and disclaimer apply

   Lightweight Read-Copy-Update implementation.
*/

#ifndef __lockless__rcu_h__
#define __lockless__rcu_h__

#include "rcu_guard.h"
#include "list.h"
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

    Rcu() : current(0)
    {
        epochs[0].init();
        epochs[1].init();
    }

    ~Rcu()
    {
        doDeferred(epochs[0].deferList.head.exchange(nullptr));
        doDeferred(epochs[1].deferList.head.exchange(nullptr));
    }

    Rcu(const Rcu&) = delete;
    Rcu& operator=(const Rcu&) = delete;


    size_t enter()
    {
        // \todo there's a race here. See GlobalRcu::enter()
        size_t epoch = current.load();
        epochs[epoch & 1].count++;

        log.log(LogRcu, "enter", "epoch=%ld, count=%ld",
                epoch, epochs[epoch & 1].count.load());

        size_t oldOther = epoch - 1;
        if (!epochs[oldOther & 1].count.load()) {
            size_t oldCurrent = epoch;
            current.compare_exchange_weak(oldCurrent, oldCurrent + 1);
        }

        return epoch;
    }


    void exit(size_t epoch)
    {
        auto& ep = epochs[epoch & 1];

        size_t oldCount = ep.count.load();
        log.log(LogRcu, "exit", "epoch=%ld, count=%ld", epoch, oldCount);
        locklessCheckGt(oldCount, 0ULL, log);


        /* Note that we can't decrement the counter before we write the head of
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
        if (oldCount == 1) {
            // I don't think this needs to be atomic.
            deferHead = ep.deferList.head.exchange(nullptr);

            log.log(LogRcu, "exit-defer", "epoch=%ld, head=%p",
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
        size_t epoch = current.load();
        epochs[epoch & 1].deferList.push(node);

        log.log(LogRcu, "add-defer", "epoch=%ld, head=%p", epoch, node);
    }

    std::string print() const
    {
        size_t oldCurrent = current.load();
        size_t oldOther = oldCurrent - 1;

        std::array<char, 80> buffer;
        snprintf(buffer.data(), buffer.size(), "{ cur=%ld, count=[%ld, %ld] }",
                oldCurrent,
                epochs[oldCurrent & 1].count.load(),
                epochs[oldOther & 1].count.load());

        return std::string(buffer.data());
    }

private:

    void doDeferred(ListNode<DeferFn>* node)
    {
        log.log(LogRcu, "do-defer", "head=%p", node);

        while (node) {
            node->value();

            auto* next = node->next();
            delete node;
            node = next;
        }
    }


    // \todo Align to a cache line.
    struct Epoch
    {
        std::atomic<size_t> count;
        List<DeferFn> deferList;

        void init()
        {
            count.store(0);
            deferList.head = nullptr;
        }
    };


    std::atomic<size_t> current; // \todo align to a cache line.
    Epoch epochs[2];

public:

    DebuggingLog<10240, DebugRcu>::type log;

};

} // namespace lockless

#endif // __lockless__rcu_h__
