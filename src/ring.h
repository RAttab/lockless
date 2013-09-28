/* ring.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 28 Sep 2013
   FreeBSD-style copyright and disclaimer apply

   Lock-free ring queue.

   Note that you can pick and choose the pop and push functions from RingSRSW
   and RingMRMW to get RingSRMW and RingMRSR.

   \todo The name is kinda bad as it could also a ring buffer where we overwrite
   when the ring is full. Take Log as an example.

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



/******************************************************************************/
/* RING SRSW                                                                  */
/******************************************************************************/

template<typename T, size_t Size>
struct RingSRSW : public RingBase<T, Size>
{
    using RingBase<T, Size>::d;
    using RingBase<T, Size>::ring;

    bool push(T obj)
    {
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
struct RingMRMW : public RingBase<T, Size>
{
    using RingBase<T, Size>::d;
    using RingBase<T, Size>::ring;

    bool push(T obj)
    {
        locklessCheck(obj, NullLog);

        uint32_t pos = d.split.write;

        while (true) {
            T old = ring[pos % Size];

            if (!old && ring[pos % Size].compare_exchange_strong(old, obj)) {
                if (d.split.write == pos)
                    d.split.write.compare_exchange_strong(pos, pos + 1);
                return true;
            }

            if ((pos % Size) == (d.split.read % Size))
                return false;

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

            if (pos == d.split.write)
                return T(0);

            if (d.split.read == pos)
                d.split.read.compare_exchange_strong(pos, pos + 1);
        }
    }

};


} // lockless

#endif // __lockless__ring_h__
