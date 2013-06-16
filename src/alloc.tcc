/* alloc.tcc                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 15 Jun 2013
   FreeBSD-style copyright and disclaimer apply

   Allocator template implementation.

   \todo Clean-up pages after a thread dies and all its blocks are free-ed. When
   thread is destroyed set a flag in each page, check if it's all free-ed and,
   if it is, set another flag and free the page. On free, set the free flag,
   check if delete flag is up, check if the page is empty and if it is, set the
   other flag and free the page. First flag signals that no new alloc will take
   place. The second flag should probably be some kind of ref-count or rcu.

   \todo When Page::alloc allocates the last block, it should return a flag so
   that allocBlock can immediately move it to the recycle queue. Should save
   wasted scan. on the next alloc.

   \todo If allocQueue gets too big then we should start reclaiming pages. Kinda
   tricky because free can't play with these data-structures.

*/

#include "check.h"
#include "alloc.h"

#include <array>
#include <atomic>
#include <algorithm>


namespace lockless {
namespace details {


/******************************************************************************/
/* BLOCK PAGE                                                                 */
/******************************************************************************/

/** Chunk of memory that contains the blocks to be allocated along with the
    data-structure to keep track of those blocks.
 */
template<typename Policy>
struct BlockPage
{
    /** Math. That's what enums are for right? */
    enum
    {
        TotalBlocks = CeilDiv<Policy::PageSize, Policy::BlockSize>::value,

        // Upper bound on the size of our bitfield.
        BitfieldEstimate = CeilDiv<TotalBlocks, sizeof(uint64_t)>::value,

        MetadataSize = BitfieldEstimate * 2 + sizeof(BlockPage*),
        MetadataBlocks = CeilDiv<Policy::BlockSize, MetadataSize>::value,
        MetadataPadding = Policy::BlockSize - (MetadataSize % Policy::BlockSize),

        NumBlocks = TotalBlocks - MetadataBlocks,
        BitfieldSize = CeilDiv<NumBlocks, sizeof(uint64_t)>::value,
    };

    /** data-structure for our allocator. */
    struct Metadata
    {
        // \todo We should avoid having these two bitfields share a cacheline.
        uint64_t freeBlocks[BitfieldSize];
        std::atomic<uint64_t> recycledBlocks[BitfieldSize];

        BlockPage* next;

        uint8_t padding[MetadataPadding];
    } md;

    static_assert(
            sizeof(md) == MetadataBlocks * Policy::BlockSize,
            "Inconsistent md size");

    /** Storage for our blocks. */
    uint8_t blocks[NumBlocks][Policy::BlockSize];

    static_assert(
            (sizeof(md) + sizeof(blocks)) <= Policy::PageSize,
            "Inconsistent page size");


    void init()
    {
        // may want to go for a memset here.
        fill(begin(md.freeBlocks), end(md.freeBlocks), -1ULL);
        fill(begin(md.recycledBlocks), end(md.recycledBlocks), 0ULL);
        md.next = nullptr;
    }

    static BlockPage<Policy>* create()
    {
        auto* page = alignedMalloc<BlockPage<Policy>*>(
                Policy::PageSize, Policy::PageSize);

        page->init();
        return page;
    }


    void* allocFromBitfield(size_t index)
    {
        size_t subIndex = lsb(md.freeBlocks[index]);
        size_t block = index * sizeof(uint64_t) + subIndex;

        if (block >= NumBlocks) return nullptr;

        md.freeBlocks[index] |= 1 << subIndex;
        return reinterpret_cast<void*> (&blocks[block]);
    }

    /** Can be called from a single thread only but could still need synchronize
        to access the recycledBlock.

        Note that the 2-pass scan has the nice advantage that we're more likely
        to maintain spatial locality. It also allows us to do a quick
        synchronization-free scan to look for free blocks.

        \todo These scans could easily be vectorized. Templating could render
        this a bit tricky.
     */
    void* alloc()
    {
        // Do a synchronization-free pass through the alloc list.
        for (size_t i = 0; i < BitfieldSize; ++i) {
            if (!md.freeBlocks[i]) continue;

            void* ptr = allocFromBitfield(i);
            if (ptr) return ptr;
        }

        // Nothing left, let's check
        for (size_t i = 0; i < BitfieldSize; ++i) {
            if (!md.recycledBlocks[i]) continue;

            md.freeBlocks[i] |= md.recycledBlocks[i].exchange(0ULL);

            void* ptr = allocFromBitfield(i);
            locklessCheck(ptr, NullLog);
            return ptr;
        }

        return nullptr;
    }


    /** Could be called from multiple threads and should therefor not manipulate
        the freeBlocks bitfield. Instead we only manipulate the recycledBlocks
        bitfield which alloc will apply in batch back into the freeBlocks
        bitfield.

        This scheme has the effect of reducing the amount of synchronization
        between the allocation thread and the deallocation threads.
     */
    void free(void* ptr)
    {
        locklessCheckGt(ptr, this, NullLog);
        locklessCheckLt(ptr, this + sizeof(*this), NullLog);

        size_t block = (ptr - this) / Policy::BlockSize - MetadataBlocks;
        locklessCheckLt(block, NumBlocks, NullLog);

        size_t topIndex = block / sizeof(uint64_t);
        size_t subIndex = block % sizeof(uint64_t);

        md.recycledBlocks[topIndex] |= 1ULL << subIndex;
    }

    static BlockPage<Policy>* pageForBlock(void* block)
    {
        return reinterpret_cast<BlockPage<Policy>*>(
                block & ~(Policy::PageSize - 1));
    }
};


/******************************************************************************/
/* BLOCK QUEUE                                                                */
/******************************************************************************/

/** Single-threaded queue to manage our pages. */
template<typename Policy>
struct BlockQueue
{
    typedef BlockPage<Policy> Page;

    BlockQueue() : head(nullptr), tail(nullptr) {}

    Page* peek() const
    {
        return head;
    }

    void pop()
    {
        head = head->next;
        if (!head) tail = nullptr;
    }

    void remove(Page* page, Page* prev)
    {
        if (page == head) {
            pop();
            return;
        }

        locklessCheck(prev, NullLog);
        prev->next = page->next;
        if (page == tail) tail = prev;
    }

    void pushFront(Page* page)
    {
        page->next = head;
        head = page;
        if (!tail) tail = head;
    }

    void pushBack(Page* page)
    {
        if (tail)
            tail = tail->next = page;
        pushFront(page);
    }

    Page* head;
    Page* tail;
};

} // namespace details


/******************************************************************************/
/* BLOCK ALLOC                                                                */
/******************************************************************************/

template<typename Policy, typename Tag>
Tls<details::BlockQueue<Policy>, Tag>
BlockAlloc<Policy, Tag>::
allocQueue;

template<typename Policy, typename Tag>
Tls<details::BlockQueue<Policy>, Tag>
BlockAlloc<Policy, Tag>::
recycleQueue;


template<typename Policy, typename Tag>
void*
BlockAlloc<Policy, Tag>::
allocBlock()
{
    // Attempt to allocate from the alloc queue.
    auto* page = allocQueue.peek();
    if (page) {
        void* ptr = page->alloc();
        if (ptr) return ptr;

        // No blocks left to allocate, move it to the recycle queue and wait for
        // a few blocks to free up.
        allocQueue.pop();
        recycleQueue.pushBack(page);
    }


    /** We don't want to always check the head of the recycle queue otherwise a
        single full page at the head could force us to do a linear scan through
        the entire queue to find a block. That's not good. Instead we maintain a
        cursor that progressively moves through the recycle queue looking for a
        page with some free block.

        \todo This could get a little slow if a page is full of rarely released
        blocks. To help out we could add another queue for pages that failed to
        yield a block in the recycled queue. Kinda like a generational GC.
    */
    auto* prev = nextRecycledPage;
    page = prev ? prev->next : recycleQueue.peek();

    if (page) {
        void* ptr = page->alloc();

        // If we found a block then move it back to the alloc list since it
        // likely contains more available blocks.
        if (ptr) {
            recycleQueue.remove(page, prev);
            allocQueue.pushBack(page);
            return ptr;
        }

        // Oh well, move the cursor to the next page.
        nextRecycledPage = page;
    }

    // Couldn't find a block in our queues; create a new page.
    page = details::BlockPage<Policy>::create();
    return page ? page->alloc() : nullptr;
}


/** Free returns the block to the thread that allocated it which means that this
    function has to be thread-safe. To keep the allocator as simple as possible,
    free can't manipulate the allocation queues which means that allocBlock()
    can is almost entirely single-threaded.
 */
template<typename Policy, typename Tag>
void
BlockAlloc<Policy, Tag>::
freeBlock(void* block)
{
    details::BlockPage<Policy>::pageForBlock(block)->free(block);
}


} // lockless
