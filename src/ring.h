/* ring.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 28 Sep 2013
   FreeBSD-style copyright and disclaimer apply

   Lock-free ring queue.

   Note that you can pick and choose the pop and push functions from
   RingQueueSRSW and RingQueueMRMW to get RingQueueSRMW and RingQueueMRSR.

   \todo Could probably juice out extra performance by splitting the read and
   write cursors on different cache lines but we would lose our ability to have
   useful size metrics. Also, no amount of optimization will make any single
   queue scale (not entirely true but the alternative isn't a queue in the
   traditional sense and requires an op to wait for an oposite op to take place
   before it can continue).

 */

#ifndef __lockless__ring_h__
#define __lockless__ring_h__

#include "check.h"

#include <array>
#include <atomic>
#include <sstream>


namespace lockless {


/******************************************************************************/
/* RING BASE                                                                  */
/******************************************************************************/

namespace details {

template<typename T, size_t Size_>
struct RingBase
{
    RingBase()
    {
        d.all = 0;
        for (auto& val : ring) val = T(0);
    }

    locklessEnum size_t Size = Size_;
    locklessStaticAssert(Size > 0);
    locklessStaticAssert(Size <= (uint32_t(0) - 1));

    constexpr size_t capacity() const { return Size; }

    size_t size() const
    {
        uint64_t all = d.all;
        uint32_t rpos = all;
        uint32_t wpos = all >> 32;

        return wpos - rpos;
    }

    bool empty() const
    {
        uint64_t all = d.all;
        uint32_t rpos = all;
        uint32_t wpos = all >> 32;

        return rpos == wpos;
    }

    std::string dump() const
    {
        std::stringstream ss;

        ss << format("{ w=%x, r=%x, [ ", d.split.write, d.split.read);

        for (size_t i = 0; i < Size; ++i)
            ss << format("%lld:%p ", i, ring[i]);

        ss << "] }";
        return ss.str();
    }

protected:

    void inc(std::atomic<uint32_t>& cursor, uint32_t& pos)
    {
        if (cursor == pos)
            cursor.compare_exchange_strong(pos, pos + 1);
        else pos = cursor;
    }

    std::array<std::atomic<T>, Size> ring;
    union {
        struct {
            std::atomic<uint32_t> read;
            std::atomic<uint32_t> write;
        } split;
        std::atomic<uint64_t> all;
    } d;

};

} // namespace details


/******************************************************************************/
/* RING QUEUE SRMW                                                            */
/******************************************************************************/

template<typename T, size_t Size>
struct RingQueueSRSW : public details::RingBase<T, Size>
{
    using details::RingBase<T, Size>::d;
    using details::RingBase<T, Size>::ring;

    bool push(T obj)
    {
        // Null is a reserved value used by the algorithm to indicate that a
        // slot is empty.
        locklessCheck(obj, NullLog);

        uint32_t pos = d.split.write;

        if (ring[pos % Size]) return false;
        ring[pos % Size] = obj;

        d.split.write = pos + 1;
        return true;
    }

    T pop()
    {
        uint32_t pos = d.split.read;

        T value = ring[pos % Size];
        if (!value) return T(0);
        ring[pos % Size] = T(0);

        d.split.read = pos + 1;
        return value;
    }

};


/******************************************************************************/
/* RING QUEUE MRMW                                                            */
/******************************************************************************/

template<typename T, size_t Size>
struct RingQueueMRMW : public details::RingBase<T, Size>
{
    using details::RingBase<T, Size>::d;
    using details::RingBase<T, Size>::ring;

    bool push(T obj)
    {
        // Null is a reserved value used by the algorithm to indicate that a
        // slot is empty.
        locklessCheck(obj, NullLog);

        bool done = false;
        uint32_t pos = d.split.write;

        while (!done) {
            if (pos - d.split.read == Size) return false;

            T old = ring[pos % Size];
            done = !old && ring[pos % Size].compare_exchange_strong(old, obj);

            this->inc(d.split.write, pos);
        }

        return true;
    }

    T pop()
    {
        T old;
        bool done = false;
        uint32_t pos = d.split.read;

        while (!done) {
            if (pos == d.split.write) return T(0);

            old = ring[pos % Size];
            done = old && ring[pos % Size].compare_exchange_strong(old, T(0));

            this->inc(d.split.read, pos);
        }

        return old;
    }

};


/******************************************************************************/
/* RING BUFFER                                                                */
/******************************************************************************/

/** Same as a ring queue except that that push can never fail. If the ring
    happens to be full then the tail of the queue is popped and discarded.

    \todo would be nice if overwritting the tail didn't require an actual pop
    op. Unfortunately, it's kind of required to ensure that the slot isn't read
    by another thread while we're writting a new head.

    \todo This actually leaks memory when we discard the head and T happens to
    be a pointer. Need to find an elegant way around this problem.

 */
template<typename T, size_t Size>
struct RingBuffer : public details::RingBase<T, Size>
{
    using details::RingBase<T, Size>::d;
    using details::RingBase<T, Size>::ring;

    void push(T obj)
    {
        // Null is a reserved value used by the algorithm to indicate that a
        // slot is empty.
        locklessCheck(obj, NullLog);

        bool done = false;
        uint32_t wpos = d.split.write;

        while (!done) {
            // We're about to overwrite this value so make sure it can't be read
            uint32_t rpos = d.split.read;
            if (wpos - rpos == Size) pop(rpos);

            T old = ring[wpos % Size];
            done = !old && ring[wpos % Size].compare_exchange_strong(old, obj);

            this->inc(d.split.write, wpos);
        }
    }

    // Same implementation as RingQueueMRMW just split up a bit.
    T pop()
    {
        T old(0);
        uint32_t pos = d.split.read;

        do {
            if (pos == d.split.write) return T(0);
        } while(!(old = pop(pos)));

        return old;
    }

private:

    T pop(uint32_t& pos)
    {
        T old = ring[pos % Size];
        bool done = old && ring[pos % Size].compare_exchange_strong(old, T(0));

        this->inc(d.split.read, pos);
        return done ? old : T(0);
    }

};


} // lockless

#endif // __lockless__ring_h__
