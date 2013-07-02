/* alloc.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 15 Jun 2013
   FreeBSD-style copyright and disclaimer apply

   Lock-free allocators.

   We should try to guaratee no-throw specs in at least BlockAlloc. Then we can
   make a NoThrowBlockAllocT which would give us nice nothrow guarantees in our
   allocator.

 */

#ifndef __lockless__alloc_h__
#define __lockless__alloc_h__


#include "arch.h"
#include "tls.h"
#include "alloc_utils.h"


namespace lockless {


/******************************************************************************/
/* ALIGNED MALLOC                                                             */
/******************************************************************************/

void* alignedMalloc(size_t size, size_t allign);
void alignedFree(void* ptr);

template<typename T, typename... Args>
T* alignedMalloc(size_t align, Args&&... args)
{
    void* ptr = alignedMalloc(sizeof(T), align);
    new (ptr) T(std::forward<Args>(args)...);
    return reinterpret_cast<T*>(ptr);
}


/******************************************************************************/
/* ALLOC POLICY                                                               */
/******************************************************************************/

template<size_t Size>
struct PackedAllocPolicy
{
    locklessStaticAssert(Size > 0);

    locklessEnum size_t BlockSize = Size;
    locklessEnum size_t PageSize  = details::CalcPageSize<BlockSize, 64>::value;
};


template<size_t Size, size_t Align = 8>
struct AlignedAllocPolicy
{
    locklessStaticAssert(Size > 0);

    locklessEnum size_t BlockSize = CeilDiv<Size, Align>::value * Align;
    locklessEnum size_t PageSize  = details::CalcPageSize<BlockSize, 64>::value;
};


/******************************************************************************/
/* BLOCK ALLOC                                                                */
/******************************************************************************/

namespace details { template<typename Policy> struct BlockAllocTls; }


template<typename Policy, typename Tag>
struct BlockAlloc
{
    static void* allocBlock()
    {
        return allocator->allocBlock();
    }

    static void freeBlock(void* block)
    {
        allocator->freeBlock(block);
    }

private:

    static Tls<details::BlockAllocTls<Policy>, Tag> allocator;
};


/******************************************************************************/
/* BLOCK ALLOC T                                                              */
/******************************************************************************/

template<typename T, typename Policy = AlignedAllocPolicy<sizeof(T)> >
struct BlockAllocT
{

    void* operator new(size_t size)
    {
        return Allocator::allocBlock();
    }

    void operator delete(void* block)
    {
        Allocator::freeBlock(block);
    }

private:
    typedef BlockAlloc<Policy, T> Allocator;
};


} // lockless

#include "alloc.tcc"

#endif // __lockless__alloc_h__
