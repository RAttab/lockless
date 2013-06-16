/* alloc.tcc                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 15 Jun 2013
   FreeBSD-style copyright and disclaimer apply

   Allocator template implementation.

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
    static_assert(
            (Policy::PageSize - 1) & Policy::PageSize == 0,
            "Policy::PageSize must be an exponent of 2");

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
        // \todo Should be on a different cache line then the next 2 fields.
        uint64_t freeBlocks[BitfieldSize];

        // These two could be on the same cacheline
        std::atomic<uint64_t> recycledBlocks[BitfieldSize];
        std::atomic<uint64_t> refCount;

        BlockPage* next;

        uint8_t padding[MetadataPadding];
    } md;


    /** Storage for our blocks. */
    uint8_t blocks[NumBlocks][Policy::BlockSize];


    static_assert(
            sizeof(md) == MetadataBlocks * Policy::BlockSize,
            "Inconsistent metadata size");
    static_assert(
            (sizeof(md) + sizeof(blocks)) <= Policy::PageSize,
            "Inconsistent page size");


    void init()
    {
        // may want to go for a memset here.
        fill(begin(md.freeBlocks), end(md.freeBlocks), -1ULL);
        fill(begin(md.recycledBlocks), end(md.recycledBlocks), 0ULL);
        md.next = nullptr;
        md.refCount = 1ULL << 63;
    }

    static BlockPage<Policy>* create()
    {
        auto* page = alignedMalloc<BlockPage<Policy>*>(
                Policy::PageSize, Policy::PageSize);

        if (page) page->init();
        return page;
    }


    BlockPage<Policy>* next() const { return md.next; }
    void next(BlockPage<Policy>* page) { return md.next = page; }


    size_t findFreeBlockInBitfield(size_t index)
    {
        size_t subIndex = lsb(md.freeBlocks[index]);
        size_t block = index * sizeof(uint64_t) + subIndex;

        return block < NumBlocks ? block : -1ULL;
    }

    /** Can be called from a single thread only but could still need synchronize
        to access the recycledBlock.

        Note that the 2-pass scan has the nice advantage that we're more likely
        to maintain spatial locality. It also allows us to do a quick
        synchronization-free scan to look for free blocks.

        \todo These scans could easily be vectorized. Templating could render
        this a bit tricky.
     */
    size_t findFreeBlock()
    {
        for (size_t i = 0; i < BitfieldSize; ++i) {
            if (!md.freeBlocks[i]) continue;

            size_t block = findFreeBlockInBitfield(i);
            if (block != -1ULL) return block;
        }

        for (size_t i = 0; i < BitfieldSize; ++i) {
            if (!md.recycledBlocks[i]) continue;

            md.freeBlocks[i] |= md.recycledBlocks[i].exchange(0ULL);

            size_t block = findFreeBlockInBitfield(i);
            if (block != -1ULL) return block;
        }

        return -1;
    }

    bool hasFreeBlock()
    {
        return findFreeBlock() != -1ULL;
    }

    /** Completely wait-free allocation of a block. */
    void* alloc()
    {
        size_t block = findFreeBlock();
        if (block == -1ULL) return nullptr;

        size_t topIndex = block / sizeof(uint64_t);
        size_t subIndex = block % sizeof(uint64_t);

        md.freeBlocks[topIndex] |= 1ULL << subIndex;
        return reinterpret_cast<void*>(&blocks[block]);
    }


    /** These two functions manages the refCount such that we can safely delete
        the page after the function kill() is called.

        Note that this is the only lock-free algorithm in this allocator.
        Everything else is wait-free. This really sucks but I don't think it's
        possible to implement this wait-free. On the bright side, this is only
        used when deallocating which means that allocation are still wait-free.
     */
    void enterFree() { md.refCount++; }
    void exitFree()
    {
        bool freePage = false;
        size_t oldCount = md.refCount;

        do {
            // Was killed called?
            if (oldCount & (1ULL << 63)) continue;

            // Are all the blocks deallocated?
            freePage = true;
            for (size_t i = 0; freePage && i < BitfieldSize; ++i) {
                md.recycledBlocks[i] |= md.freeBlocks[i];
                if (md.recycledBlocks != -1ULL) freePage = false;
            }

        } while (md.refCount.compare_exchange_strong(oldCount, oldCount - 1));

        /** If freePage is set then all blocks in this page have been freed and
            there will therefor not be any other threads that can increment
            refCount. If refCount has reached 0 then we're the last thread
            within this page and since no other threads can access this page
            then it's safe to delete the page.
         */
        if (freePage && oldCount == 1)
            alignedFree(this);
    }


    /** Indicates that the page will no longer be used for allocation and that
        it should be reclaimed whenever it is safe to do so.
     */
    void kill()
    {
        enterFree();
        md.refCount &= ~(1ULL << 63);
        exitFree();
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

        enterFree();
        md.recycledBlocks[topIndex] |= 1ULL << subIndex;
        exitFree();
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
        head = head->next();
        if (!head) tail = nullptr;
    }

    void remove(Page* page, Page* prev)
    {
        if (page == head) {
            pop();
            return;
        }

        locklessCheck(prev, NullLog);
        prev->next(page->next());
        if (page == tail) tail = prev;
    }

    void pushFront(Page* page)
    {
        page->next(head);
        head = page;
        if (!tail) tail = head;
    }

    void pushBack(Page* page)
    {
        if (tail) {
            tail->next(page);
            tail = page;
        }
        pushFront(page);
    }

    Page* head;
    Page* tail;
};


/******************************************************************************/
/* BLOCK ALLOC TLS                                                            */
/******************************************************************************/

template<typename Policy>
struct BlockAllocTls
{
    typedef BlockPage<Policy> Page;

    BlockQueue<Policy> allocQueue;
    BlockQueue<Policy> recycledQueue;
    Page* nextRecycledPage;

    BlockAllocTls() : nextRecycledPage(nullptr) {}

    ~BlockAllocTls()
    {
        Page* page;

        while (page = allocQueue.peek()) {
            allocQueue.pop();
            page->kill();
        }

        while (page = recycledQueue.peek()) {
            recycledQueue.pop();
            page->kill();
        }
    }

    void* allocBlock()
    {
        bool hasMore;
        void* ptr;

        /** Check to see if we can move a recycled page back into the alloc
            queue.

            We don't want to always check the head of the recycle queue
            otherwise a single full page at the head could force us to do a
            linear scan through the entire queue to find a block. That's not
            good. Instead we maintain a cursor that progressively moves through
            the recycle queue looking for a page with some free block.

            Note that while we could call alloc() instead of hasFreeBlock() this
            would degrade the spatial locality of our allocator.

            \todo This could get a little slow if the queue is full of pages
            that are full. To help out we could add another queue for pages that
            failed to yield a block in the recycled queue. Kinda like a
            generational GC.
        */
        Page* prev = nextRecycledPage;
        Page* page = prev ? prev->next() : recycledQueue.peek();

        if (page) {
            if (page->hasFreeBlock()) {
                recycledQueue.remove(page, prev);
                allocQueue.pushBack(page);
            }
            nextRecycledPage = page;
        }


        /** Alrighty, time to allocate a block. */
        page = allocQueue.peek();
        if (page) {
            void* ptr = page->alloc();

            if (!page->hasFreeBlock()) {
                allocQueue.pop();
                recycledQueue.pushBack(page);
            }

            // Invariant states that allocQueue should either be empty or a
            // block is available in the head.
            locklessCheck(ptr, NullLog);
            return ptr;
        }


        // Couldn't find a block so create create a new page.
        page = details::BlockPage<Policy>::create();
        if (!page) return nullptr;

        allocQueue.pushFront(page);
        return page->alloc();
    }


    /** Free returns the block to the thread that allocated it which means that
        this function has to be thread-safe. To keep the allocator as simple as
        possible, free can't manipulate the allocation queues which means that
        allocBlock() can is almost entirely single-threaded.
    */
    void freeBlock(void* block)
    {
        details::BlockPage<Policy>::pageForBlock(block)->free(block);
    }
};

} // namespace details


/******************************************************************************/
/* BLOCK ALLOC                                                                */
/******************************************************************************/

template<typename Policy, typename Tag>
Tls<details::BlockAllocTls<Policy>, Tag>
BlockAlloc<Policy, Tag>::
allocator;


} // lockless
