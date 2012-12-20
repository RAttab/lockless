/** queue.h                                 -*- C++ -*-
    Rémi Attab (remi.attab@gmail.com), 08 Dec 2012
    FreeBSD-style copyright and disclaimer apply

    Unbounded lock free queue.
*/

#ifndef __lockless_queue_h__
#define __lockless_queue_h__

#include "rcu.h"

#include <cassert>
#include <atomic>

namespace lockless {


/******************************************************************************/
/* QUEUE                                                                      */
/******************************************************************************/

/* Blah

 */
template<typename T>
struct Queue
{
    /* Blah

       Exception Safety: Only throws on calls to new.
     */
    Queue() : rcu()
    {
        assert(head.is_lock_free());

        Entry* sentinel = new Entry();
        head.store(sentinel);
        tail.store(sentinel);
    }


    /* Blah

       Thread Safety: Can issue calls to new or delete which could
           lock. Everything else is lock-free and wait-free.

       Exception Safety: Only throws on calls to new or delete.
     */
    template<typename T2>
    void push(T2&& value)
    {
        RcuGuard guard(rcu);

        Entry* entry = new Entry(std::forward(value));

        Entry* oldTail = tail.load();
        while(true) {
            Entry* oldNext = oldTail->next.load();

            if (!oldNext) {
                if (!oldTail.compare_exchange_weak(oldNext, entry))
                    continue;

                // If this fails then someone else updated the pointer (see
                // below) so we don't care about the outcome.
                tail.compare_exchange_strong(oldTail, entry);
                return;
            }

            // Someone beat us to the enqueue so ensure that the tail is
            // properly updated before continuing.
            tail.compare_exchange_strong(oldTail, oldNext);
        }
    }


    /* Blah

       Thread Safety: Can issue calls to new or delete which could
           lock. Everything else is lock-free and wait-free.

       Exception Safety: Only throws on calls delete.
     */
    std::pair<bool, T> peek()
    {
        RcuGuard guard(rcu);

        while (true) {
            Entry* oldHead = head.load();
            Entry* oldTail = tail.load();

            if (oldHead != oldTail)
                return { true, oldHead->value };

            Entry* oldNext = oldTail->next.load();

            if (!oldNext) return { false, T() };

            // tail is lagging behind so help move it forward.
            tail.compare_exchange_strong(oldTail, oldNext);
        }
    }


    /* Blah

       Thread Safety: Can issue calls to new or delete which could
           lock. Everything else is lock-free and wait-free.

       Exception Safety: Only throws on calls to new or delete.
     */
    std::pair<bool, T> pop()
    {
        RcuGuard guard(rcu);

        Entry* oldHead = head.load();

        while(true) {
            Entry* oldTail = tail.load();

            if (oldHead == oldTail) {
                Entry* oldNext = oldTail->next.load();

                if (!oldNext) return { false, T() };

                // tail is lagging behind so help move it forward.
                tail.compare_exchange_strong(oldTail, oldNext);
            }

            else {
                Entry* oldNext = oldHead->next.load();
                if (!head.compare_exchange_weak(oldHead, oldNext)) continue;

                T value = oldHead->value;
                rcu.defer([=] { delete oldHead; });
                return { true, oldHead->value };
            }
        }
    }

private:

    struct Entry
    {
        Entry() : value(), next(0) {}

        template<typename T2>
        Entry(T2&& newValue) :
            value(std::forward(newValue)),
            next(0)
        {}

        T value;
        std::atomic<Entry*> next;
    };

    Rcu rcu;
    std::atomic<Entry*> head;
    std::atomic<Entry*> tail;
};

} // namespace lockless

#endif // __lockless_queue_h__
