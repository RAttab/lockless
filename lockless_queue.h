/** lockless_queue.h                                 -*- C++ -*-
    Rémi Attab, 08 Dec 2012
    FreeBSD

    Unbounded lock free queue.

    Well not quite because it might block on calls to new and delete. These only
    happen once per operation and either at the very begining for push() or at
    the very end for pop().

*/

#ifndef __lockless_queue_h__
#define __lockless_queue_h__

#include <cassert>
#include <atomic>
#include <memory>

namespace lockless {

/******************************************************************************/
/* QUEUE                                                                      */
/******************************************************************************/

template<typename T>
struct Queue
{

private:

    struct Entry
    {
        Entry() : value(), next(0) {}

        Entry(const T& newValue) :
            value(newValue), next(0)
        {}

        Entry(T&& newValue) :
            value(std::move(newValue)), next(0)
        {}

        T value;
        std::atomic<std::shared_ptr<Entry> > next;
    };


public:

    Queue()
    {
        assert(std::atomic<std::shared_ptr<Entry> >().is_lock_free());

        std::shared_ptr<Entry> sentinel = new Entry();
        head.store(sentinel);
        tail.store(sentinel);
    }

    void push(const T& value)
    {
        pushImpl(std::make_shared<Entry>(value));
    }

    void push(T&& value)
    {
        pushImpl(std::make_shared<Entry>(std::move(value)));
    }

    std::pair<bool, T>
    peak()
    {
        while (true) {
            std::shared_ptr<Entry> oldHead = head.load();
            std::shared_ptr<Entry> oldTail = tail.load();

            if (oldHead != oldTail)
                return { true, oldHead->value };

            else {
                std::shared_ptr<Entry> oldNext = oldTail->next.load();

                if (!oldNext) return { false, T() };

                // tail is lagging behind so help move it forward.
                tail.compare_exchange_strong(oldTail, oldNext);
            }
        }
    }

    std::pair<bool, T>
    pop()
    {
        std::shared_ptr<Entry> oldHead = head.load();

        while(true) {
            std::shared_ptr<Entry> oldTail = tail.load();

            if (oldHead == oldTail) {
                std::shared_ptr<Entry> oldNext = oldTail->next.load();

                if (!oldNext) return { false, T() };

                // tail is lagging behind so help move it forward.
                tail.compare_exchange_strong(oldTail, oldNext);
            }

            else {
                std::shared_ptr<Entry> next = oldHead->next.load();
                if (head.compare_exchange_weak(oldHead, next))
                    return { true, oldHead->value };
            }
        }
    }

private:

    void pushImpl(std::shared_ptr<Entry> entry)
    {
        std::shared_ptr<Entry> oldTail = tail.load();
        while(true) {
            std::shared_ptr<Entry> oldNext = oldTail->next.load();

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

    std::atomic<std::shared_ptr<Entry> > head;
    std::atomic<std::shared_ptr<Entry> > tail;
};

} // namespace lockless

#endif // __lockless_queue_h__
