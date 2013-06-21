/* alloc_utils.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 16 Jun 2013
   FreeBSD-style copyright and disclaimer apply

   Utilities for the allocators.
*/

#ifndef __lockless__alloc_utils_h__
#define __lockless__alloc_utils_h__

#include "utils.h"
#include "log.h"
#include "check.h"

namespace lockless {


/******************************************************************************/
/* PAGE CALCULATORS                                                           */
/******************************************************************************/

namespace details {

template<size_t Size>
struct CalcPageAlign
{
    locklessEnum size_t multiplier = CalcPageAlign<Size / 2>::multiplier * 2;
    locklessEnum size_t value = multiplier * PageSize;
};

template<> struct CalcPageAlign<0>
{
    locklessEnum size_t multiplier = 1;
    locklessEnum size_t value = PageSize;
};

template<size_t BlockSize, size_t MinBlocks>
struct CalcPageSize
{
    // Ensures that we have enough pages to store at least MinBlocks blocks.
    locklessEnum size_t unaligned =
        CeilDiv<BlockSize * MinBlocks, PageSize>::value * PageSize;

    // Make sure we can easily find the header of our page.
    locklessEnum size_t value = CalcPageAlign<unaligned / PageSize>::value;
};

} // namespace details
} // lockless

#endif // __lockless__alloc_utils_h__
