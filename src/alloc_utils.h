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
    locklessEnum size_t multiplier =
        CalcPageAlign<CeilDiv<Size, 2>::value>::multiplier * 2;
};

template<> struct CalcPageAlign<1>
{
    locklessEnum size_t multiplier = 1;
};

template<size_t BlockSize, size_t MinBlocks>
struct CalcPageSize
{
    // Ensures that we have enough pages to store at least MinBlocks blocks.
    locklessEnum size_t unaligned =
        CeilDiv<BlockSize * MinBlocks, PageSize>::value;

    // Make sure we can easily find the header of our page.
    locklessEnum size_t value = CalcPageAlign<unaligned>::multiplier * PageSize;
};

} // namespace details
} // lockless

#endif // __lockless__alloc_utils_h__
