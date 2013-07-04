/* alloc_test.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 03 Jul 2013
   FreeBSD-style copyright and disclaimer apply

   utiltiies for the allocator test.
*/

#ifndef __lockless__alloc_test_h__
#define __lockless__alloc_test_h__

#include "check.h"
#include "alloc.h"

#include <array>
#include <atomic>
#include <algorithm>

namespace lockless {


/******************************************************************************/
/* VALUE                                                                      */
/******************************************************************************/

locklessEnum uint64_t Magic = 0x0F1E2D3C4B5A6978ULL;

template<size_t Size>
struct Value
{
    Value()
    {
        allocated++;
        std::fill(v.begin(), v.end(), Magic);
    }
    ~Value()
    {
        deallocated++;
        auto pred = [] (uint64_t x) { return x == Magic; };
        locklessCheck(std::all_of(v.begin(), v.end(), pred), NullLog);
    }

    LOCKLESS_BLOCK_ALLOC_TYPED_OPS(Value<Size>)

    std::array<uint64_t, Size> v;

    static std::atomic<size_t> allocated;
    static std::atomic<size_t> deallocated;
};

template<size_t Size> std::atomic<size_t> Value<Size>::allocated(0);
template<size_t Size> std::atomic<size_t> Value<Size>::deallocated(0);

typedef Value< 1> SmallValue;
typedef Value<11> BigValue;
typedef Value<65> HugeValue;


} // lockless

#endif // __lockless__alloc_test_h__
