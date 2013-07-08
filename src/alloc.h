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


template<typename Policy_, typename Tag>
struct BlockAlloc
{
    typedef Policy_ Policy;

    static void* allocBlock() locklessMalloc locklessNeverInline
    {
        return allocator->allocBlock();
    }

    static void freeBlock(void* block) locklessNeverInline
    {
        allocator->freeBlock(block);
    }

    static LogAggregator log();

private:

    static Tls<details::BlockAllocTls<Policy>, Tag> allocator;
};


/******************************************************************************/
/* OPERATOR MACROS                                                            */
/******************************************************************************/

template<typename T>
struct DefaultBlockAlloc
{
    typedef BlockAlloc<AlignedAllocPolicy<sizeof(T)>, T> type;
};


#define LOCKLESS_BLOCK_ALLOC_OPS_IMPL(Alloc)            \
    void* operator new(size_t)                          \
    {                                                   \
        return Alloc::allocBlock();                     \
    }                                                   \
    void operator delete(void* block)                   \
    {                                                   \
        Alloc::freeBlock(block);                        \
    }


#define LOCKLESS_BLOCK_ALLOC_OPS(Policy,Tag)                    \
    LOCKLESS_BLOCK_ALLOC_OPS_IMPL(BlockAlloc<Policy,Tag>)

#define LOCKLESS_BLOCK_ALLOC_TYPED_OPS(T)                       \
    LOCKLESS_BLOCK_ALLOC_OPS_IMPL(DefaultBlockAlloc<T>::type)

} // lockless

#include "alloc.tcc"

#endif // __lockless__alloc_h__
