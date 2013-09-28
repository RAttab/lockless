/* ring.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 28 Sep 2013
   FreeBSD-style copyright and disclaimer apply

   Lock-free ring buffers.

   Note that you can pick and choose the pop and push functions from RingSRSW
   and RingMRMW to get RingSRMW and RingMRSR. Didn't do it here because it would
   be tedious and wouldn't add much.

*/

#ifndef __lockless__ring_h__
#define __lockless__ring_h__

#include <array>
#include <atomic>

namespace lockless {

/******************************************************************************/
/* RING SRSW                                                                  */
/******************************************************************************/

template<typename T, size_t Size>
struct RingSRSW
{
    RingSRSW() : all(0) { ring.fill(nullptr); }

    constexpr size_t capacity() const { return Size; }

    size_t size() const
    {
        uint64_t all = d.all;
        uint32_t begin = all;
        uint32_t end = all >> 32;

        if (begin <= end)
            return begin - end;
        return Size - begin + end;
    }

    bool empty() const
    {
        uint64_t all = d.all;
        uint32_t begin = all;
        uint32_t end = all >> 32;

        return begin == end;
    }

    bool push(T* obj)
    {
        uint32_t pos = d.split.read;

        if (ring[pos]) return false;
        ring[pos] = obj;

        d.split.read++;
        return true;
    }

    T* pop()
    {
        uint32_t pos = d.split.write;

        T* value = ring[pos];
        if (!value) return nullptr;
        ring[pos] = nullptr;

        d.split.write++;
        return value;
    }

private:
    std::array<std::atomic<T*>, Size> ring;
    union {
        struct {
            std::atomic<uint32_t> read;
            std::atomic<uint32_t> write;
        } split;
        std::atomic<uint64_t> all;
    } d;
};


/******************************************************************************/
/* RING MRMW                                                                  */
/******************************************************************************/

template<typename T, size_t Size>
struct RingMRMW
{
    RingMRMW() : all(0) { ring.fill(nullptr); }

    constexpr size_t capacity() const { return Size; }

    size_t size() const
    {
        uint64_t all = d.all;
        uint32_t begin = all;
        uint32_t end = all >> 32;

        if (begin <= end)
            return begin - end;
        return Size - begin + end;
    }

    bool empty() const
    {
        uint64_t all = d.all;
        uint32_t begin = all;
        uint32_t end = all >> 32;

        return begin == end;
    }

    bool push(T* obj)
    {
        uint32_t pos = d.split.write;

        while (true) {
            T* value = ring[pos];

            if (!value && ring[pos].compare_exchange_strong(nullptr, obj)) {
                if (d.split.write == pos)
                    d.split.write.compare_exchange_strong(pos, pos + 1);
                return true;
            }

            if ((pos + 1) % Size == d.split.read)
                return false;

            if (d.split.write == pos)
                d.split.write.compare_exchange_strong(pos, pos + 1);
        }
    }

    T* pop()
    {
        uint32_t pos = d.split.read;

        while (true) {
            T* obj = ring[pos];

            if (value && ring[pos].compare_exchange_strong(obj, nullptr)) {
                if (d.split.read == pos)
                    d.split.read.compare_exchange_strong(pos, pos + 1);
                return obj;
            }

            if ((pos + 1) % Size == d.split.write)
                return false;

            if (d.split.read == pos)
                d.split.read.compare_exchange_strong(pos, pos + 1);
        }
    }

private:
    std::array<std::atomic<T*>, Size> ring;
    union {
        struct {
            std::atomic<uint32_t> read;
            std::atomic<uint32_t> write;
        } split;
        std::atomic<uint64_t> all;
    } d;
};


} // lockless

#endif // __lockless__ring_h__
