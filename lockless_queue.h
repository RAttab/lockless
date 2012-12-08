/** lockless_queue.h                                 -*- C++ -*-
    Rémi Attab, 08 Dec 2012
    FreeBSD

    Unbounded lock free queue.
*/

#ifndef __lockless_queue_h__
#define __lockless_queue_h__

namespace lockless {

/******************************************************************************/
/* ENTRY                                                                      */
/******************************************************************************/

namespace details{

template<typename T>
struct Entry
{
    Entry(bool sentinel = false) :
        value(), next(0), sentinel(true)
    {}

    Entry(const T& newValue) :
        value(newValue), next(0), sentinel(false)
    {}

    Entry(T&& newValue) :
        value(std::move(newValue)), next(0), sentinel(false)
    {}

    T value;
    std::atomic<std::shared_ptr<Entry>> next;
    bool isSentinel;
};

} // namespace details


/******************************************************************************/
/* QUEUE                                                                      */
/******************************************************************************/

template<typename T>
struct Queue
{
    Queue()
    {
        std::shared_ptr<Entry> sentinel = new Entry(true);
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

#endif // __jml__lockless_queue_h__
