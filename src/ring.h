/* ring.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 28 Sep 2013
   FreeBSD-style copyright and disclaimer apply

   Lock-free ring queue.

   Note that you can pick and choose the pop and push functions from RingSRSW
   and RingMRMW to get RingSRMW and RingMRSR.

   \todo The name is kinda bad as it could also be a ring buffer where we
   overwrite a value when the ring is full.

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
    locklessStaticAssert(Size < (uint32_t(0) - 1));

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
/* RING SRSW                                                                  */
/******************************************************************************/

template<typename T, size_t Size>
struct RingSRSW : public details::RingBase<T, Size>
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

        d.split.write++;
        return true;
    }

    T pop()
    {
        uint32_t pos = d.split.read;

        T value = ring[pos % Size];
        if (!value) return T(0);
        ring[pos % Size] = T(0);

        d.split.read++;
        return value;
    }

};


/******************************************************************************/
/* RING MRMW                                                                  */
/******************************************************************************/

template<typename T, size_t Size>
struct RingMRMW : public details::RingBase<T, Size>
{
    using details::RingBase<T, Size>::d;
    using details::RingBase<T, Size>::ring;

    bool push(T obj)
    {
        // Null is a reserved value used by the algorithm to indicate that a
        // slot is empty.
        locklessCheck(obj, NullLog);

        uint32_t pos = d.split.write;

        while (true) {
            T old = ring[pos % Size];

            if (!old && ring[pos % Size].compare_exchange_strong(old, obj)) {
                if (d.split.write == pos)
                    d.split.write.compare_exchange_strong(pos, pos + 1);
                return true;
            }

            if (pos - d.split.read == Size) return false;

            if (d.split.write == pos)
                d.split.write.compare_exchange_strong(pos, pos + 1);
        }
    }

    T pop()
    {
        uint32_t pos = d.split.read;

        while (true) {
            T old = ring[pos % Size];

            if (old && ring[pos % Size].compare_exchange_strong(old, T(0))) {
                if (d.split.read == pos)
                    d.split.read.compare_exchange_strong(pos, pos + 1);
                return old;
            }

            if (pos == d.split.write) return T(0);

            if (d.split.read == pos)
                d.split.read.compare_exchange_strong(pos, pos + 1);
        }
    }

};


} // lockless

#endif // __lockless__ring_h__
