/** queue.h                                 -*- C++ -*-
    Rémi Attab (remi.attab@gmail.com), 08 Dec 2012
    FreeBSD-style copyright and disclaimer apply

    Unbounded lock free queue.
*/

#ifndef __lockless_queue_h__
#define __lockless_queue_h__

#include "rcu.h"
#include "log.h"
#include "check.h"

#include <atomic>

namespace lockless {

/******************************************************************************/
/* QUEUE                                                                      */
/******************************************************************************/

/* Unbounded lock free queue.

   The secret sauce of this data-structure is the sentinel node that head always
   points to which ensure that we never have to update both head and tail when
   the list goes from empty to non-empty or vice-versa. If both head and tail
   point to the same node then we know that the list is empty because head
   always points to the sentinel node. If they're not equal then we can pop an
   element by moving head forward which turns the head head into a sentinel
   node (we first copy and return its value).

   Note that the pushing into the queue is still a two step process where we
   have to update the next pointer of the last node and update the tail. The
   trick is that updating the tail can be done by any thread once the next
   pointer has been set. For a push thread, this means that if tail's next
   pointer is not null then we can move tail forward before we try to push
   again. For a pop thread, this means that if tail and head are equal but head
   has a next pointer then we can move tail forward before trying again.

   All that's left is the node reclamation which is handled by the all powerful
   RCU.
 */
template<typename T>
struct Queue
{
    Queue() : rcu()
    {
        Entry* sentinel = new Entry();
        head.store(sentinel);
        tail.store(sentinel);
    }


    template<typename T2>
    void push(T2&& value)
    {
        RcuGuard<Rcu> guard(rcu);

        Entry* entry = new Entry(std::forward<T2>(value));

        log(LogQueue, "push-0", "value=%s, entry=%p",
                std::to_string(value).c_str(), entry);

        while(true) {
            // Sentinel node ensures that old tail is not null.
            Entry* oldTail = tail.load();
            Entry* oldNext = oldTail->next.load();

            // \todo Need to test the impact of this opt.
            // Avoids spinning on a CAS in high contention scenarios.
            if (tail.load() != oldTail) continue;

            log(LogQueue, "push-1", "tail=%p, next=%p", oldTail, oldNext);

            if (!oldNext) {
                if (!oldTail->next.compare_exchange_weak(oldNext, entry))
                    continue;

                // If this fails then someone else updated the pointer (see
                // below) so we don't care about the outcome.
                tail.compare_exchange_strong(oldTail, entry);
                return;
            }

            // Someone beat us to the push so ensure that the tail is properly
            // updated before continuing.
            tail.compare_exchange_strong(oldTail, oldNext);
        }
    }


    std::pair<bool, T> peek()
    {
        RcuGuard<Rcu> guard(rcu);

        while (true) {
            Entry* oldHead = head.load();

            // There's a read dependency between tail and next where tail MUST
            // be read before next. This ensures that if tail != head then next
            // != null.
            Entry* oldTail = tail.load();
            Entry* oldNext = oldHead->next.load();

            // \todo Need to test the impact of this opt.
            // Avoids spinning on a CAS in high contention scenarios.
            if (head.load() != oldHead) continue;

            if (oldHead == oldTail) {
                // List is empty, bail.
                if (!oldNext) return { false, T() };

                // tail is lagging behind so help move it forward.
                tail.compare_exchange_weak(oldTail, oldNext);
                continue;
            }

            locklessCheck(oldNext, log);

            return { true, oldNext->value };
        }
    }


    std::pair<bool, T> pop()
    {
        RcuGuard<Rcu> guard(rcu);

        log(LogQueue, "pop-0", "");

        while(true) {
            Entry* oldHead = head.load();

            // There's a read dependency between tail and next where tail MUST
            // be read before next. This ensures that if tail != head then next
            // != null.
            Entry* oldTail = tail.load();
            Entry* oldNext = oldHead->next.load();

            // \todo Need to test the impact of this opt.
            // Avoids spinning on a CAS in high contention scenarios.
            if (head.load() != oldHead) continue;

            log(LogQueue, "pop-1", "head=%p, next=%p, tail=%p",
                    oldHead, oldNext, oldTail);

            if (oldHead == oldTail) {
                // List is empty, bail.
                if (!oldNext) return { false, T() };

                // tail is lagging behind so help move it forward.
                tail.compare_exchange_weak(oldTail, oldNext);
                continue;
            }

            // If this could be false then the list would be empty.
            locklessCheck(oldNext, log);

            // Move the head forward.
            if (!head.compare_exchange_weak(oldHead, oldNext))
                continue;

            // Element successfully poped. oldNext is now the new sentinel so
            // copy its value and delete the old sentinel oldHead.
            T value = std::move(oldNext->value);

            log(LogQueue, "pop-2", "value=%s, entry=%p",
                    std::to_string(value).c_str(), oldNext);

            rcu.defer([=] { delete oldHead; });
            return { true, value };
        }
    }

private:

    struct Entry
    {
        Entry() : value(), next(0) {}

        template<typename T2>
        Entry(T2&& newValue) :
            value(std::forward<T2>(newValue)),
            next(0)
        {}

        T value;
        std::atomic<Entry*> next;
    };

    std::atomic<Entry*> head;
    std::atomic<Entry*> tail;
    Rcu rcu;

public:

    DebuggingLog<1000, DebugQueue>::type log;
    LogAggregator allLogs() { return LogAggregator(log, rcu.log); }

};

} // namespace lockless

#endif // __lockless_queue_h__
