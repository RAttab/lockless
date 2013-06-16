/* alloc.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 16 Jun 2013
   FreeBSD-style copyright and disclaimer apply

   Allocator implementation details.
*/

#include "alloc.h"

#include <stdlib.h>

namespace lockless {


/******************************************************************************/
/* ALIGNED MALLOC                                                             */
/******************************************************************************/

void* alignedMalloc(size_t size, size_t align)
{
    void* ptr;

    int res = posix_memalign(&ptr, align, size);
    locklessCheckEq(uintptr_t(ptr), uintptr_t(ptr) & ~(align - 1), NullLog);

    return res ? nullptr : ptr;
}

void alignedFree(void* ptr)
{
    free(ptr);
}


} // lockless
