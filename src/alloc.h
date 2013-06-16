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

template<typename T, typename... Args>
T* alignedMalloc(size_t align, Args&&... args)
{
    void* ptr = alignedMalloc(sizeof(T), align);
    return new (ptr) T(std::forward<Args>(args)...);
}


/******************************************************************************/
/* ALLOC POLICY                                                               */
/******************************************************************************/


template<size_t Size>
struct PackedAllocPolicy
{
    enum
    {
        BlockSize = Size,
        PageSize  = details::CalcPageSize<BlockSize, 64>::value,
    };
};


template<size_t Size, size_t Align = 8>
struct AlignedAllocPolicy
{
    enum
    {
        BlockSize = CeilDiv<Size, Align>::value * Align,
        PageSize  = details::CalcPageSize<BlockSize, 64>::value,
    };
};

namespace details {

template<typename Policy> struct BlockQueue;
template<typename Policy> struct BlockPage;

} // namespace details


/******************************************************************************/
/* BLOCK ALLOC                                                                */
/******************************************************************************/

template<typename Policy, typename Tag>
struct BlockAlloc
{
    static void* allocBlock();
    static void freeBlock(void* block);

private:

    static Tls<details::BlockQueue<Policy>, Tag> allocQueue;
    static Tls<details::BlockQueue<Policy>, Tag> recycleQueue;
    static Tls<details::BlockPage<Policy>*, Tag> nextRecycledPage;
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
