/* alloc.tcc                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 15 Jun 2013
   FreeBSD-style copyright and disclaimer apply

   Allocator template implementation.

   \todo If allocQueue gets too big then we should start reclaiming pages. Kinda
   tricky because free can't play with these data-structures.

*/

#include "check.h"
#include "log.h"
#include "alloc.h"
#include "bits.h"
#include "atomic_pod.h"

#include <array>
#include <atomic>
#include <iterator>
#include <algorithm>


namespace lockless {
namespace details {

// The log statements are too complicated to be disabled in the usual manner so
// instead we mass disable them using this macro.
#if 1
#   define log(...)
#endif


/******************************************************************************/
/* UTILS                                                                      */
/******************************************************************************/

std::string printBitfield(size_t index, uint64_t val)
{
    if (val == -1ULL) return format("%d:F ", index);
    if (val == 0ULL) return format("%d:0 ", index);
    return format("%d:%p ", index, val);
}

template<typename Array>
std::string printBitfield(const Array& arr, size_t size, uint64_t base)
{
    std::string str;

    for (size_t i = 0; i < size; ++i) {
        if (arr[i] == base) continue;
        str += printBitfield(i, arr[i]);
    }

    return str;
}

std::string printRefCount(size_t val)
{
    size_t mask = 1ULL << 63;
    if (val & mask) return std::to_string(val & ~mask);
    return format("{k,%ld}", val);
}

template<typename Policy>
struct BlockAllocLog
{
    typedef DebuggingLog<10240, DebugAlloc>::type type;
    static type log;
};

template<typename Policy>
typename BlockAllocLog<Policy>::type BlockAllocLog<Policy>::log;


/******************************************************************************/
/* BLOCK PAGE                                                                 */
/******************************************************************************/

/** Chunk of memory that contains the blocks to be allocated along with the
    data-structure to keep track of those blocks.

    \todo I'm starting to think I should do all of this dynamically... Would be
    far more flexible and wouldn't have the current padding headache...
 */
template<typename Policy>
struct BlockPage
{
    locklessStaticAssert(IsPow2<Policy::PageSize>::value);

    locklessEnum size_t TotalBlocks = Policy::PageSize / Policy::BlockSize;

    // Upper bound on the size of our bitfield.
    locklessEnum size_t BitfieldEstimate = CeilDiv<TotalBlocks, 64>::value;


    /** Metadata used to keep track of the allocator state.

        Note that since we need the struct to be packed in order to properly pad
        it, we have to use AtomicPod.

        \todo Should split up the arrays on different cache lines.
     */
    struct locklessPacked Metadata
    {
        std::array<uint64_t, BitfieldEstimate> freeBlocks;
        std::array<AtomicPod<uint64_t>, BitfieldEstimate> recycledBlocks;

        AtomicPod<uint64_t> refCount;
        BlockPage* next;
    };

    Pad<Metadata, Policy::BlockSize> md;
    locklessStaticAssert((CheckPad<Metadata, Policy::BlockSize>::value));


    locklessEnum size_t MetadataBlocks =
        CeilDiv<sizeof(md), Policy::BlockSize>::value;

    locklessEnum size_t NumBlocks = TotalBlocks - MetadataBlocks;

    // Actual bound on the size of our bitfield.
    locklessEnum size_t BitfieldSize = CeilDiv<NumBlocks, 64>::value;


    /** Storage for our blocks. */
    std::array<uint8_t[Policy::BlockSize], NumBlocks> blocks;
    locklessStaticAssert(sizeof(md) + sizeof(blocks) <= Policy::PageSize);


    void init()
    {
        // may want to go for a memset here.
        std::fill(md.freeBlocks.begin(), md.freeBlocks.end(), -1ULL);
        std::fill(md.recycledBlocks.begin(), md.recycledBlocks.end(), 0ULL);
        md.next = nullptr;
        md.refCount = 1ULL << 63;

        log(LogAlloc, "p-init", "p=%p", this);
    }

    static BlockPage<Policy>* create()
    {
        auto* page = alignedMalloc< BlockPage<Policy> >(Policy::PageSize);
        if (page) page->init();
        return page;
    }


    BlockPage<Policy>* next() const { return md.next; }
    void next(BlockPage<Policy>* page) { md.next = page; }


    size_t findFreeBlockInBitfield(size_t index)
    {
        size_t subIndex = ctz(md.freeBlocks[index]);
        size_t block = index * 64 + subIndex;

        log(LogAlloc, "p-find", "p=%p, bf=%s, sub=%ld, block=%ld",
                this, printBitfield(index, md.freeBlocks[index]).c_str(),
                subIndex, block);

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
    size_t findFreeBlock() locklessNeverInline
    {
        for (size_t i = 0; i < BitfieldSize; ++i) {
            if (!md.freeBlocks[i]) continue;

            size_t block = findFreeBlockInBitfield(i);
            if (block != -1ULL) return block;
        }

        log(LogAlloc, "p-find-rec", "p=%p", this);

        for (size_t i = 0; i < BitfieldSize; ++i) {
            if (!md.recycledBlocks[i]) continue;

            md.freeBlocks[i] |= md.recycledBlocks[i].exchange(0ULL);

            size_t block = findFreeBlockInBitfield(i);
            locklessCheckNe(block, -1ULL, log);
            return block;
        }

        log(LogAlloc, "p-find-nop", "p=%p", this);
        return -1;
    }

    bool hasFreeBlock()
    {
        return findFreeBlock() != -1ULL;
    }

    /** Completely wait-free allocation of a block. */
    void* alloc()
    {
        const size_t block = findFreeBlock();

        log(LogAlloc, "p-alloc-0", "p=%p, block=%ld %s",
                this, block, print().c_str());

        if (block == -1ULL) return nullptr;

        const size_t topIndex = block / 64;
        const size_t subIndex = block % 64;

        log(LogAlloc, "p-alloc-1", "p=%p, bf=%s, sub=%ld, ptr=%p",
                this, printBitfield(topIndex, md.freeBlocks[topIndex]).c_str(),
                subIndex, &blocks[block]);

        md.freeBlocks[topIndex] &= ~(1ULL << subIndex);
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
    bool exitFree()
    {
        bool emptyPage = false;
        size_t oldCount = md.refCount;

        do {
            log(LogAlloc, "p-exit-0", "p=%p, oldCount=%s",
                    this, printRefCount(oldCount).c_str());

            // No point on doing the expensive isEmpty check if kill() wasn't
            // called and if we're not the last person out.
            if (oldCount > 1) continue;

            log(LogAlloc, "p-exit-1", "p=%p, %s", this, print().c_str());

            // Are all the blocks deallocated?
            emptyPage = true;
            for (size_t i = 0; emptyPage && i < BitfieldSize; ++i) {
                md.recycledBlocks[i] |= md.freeBlocks[i];
                if (md.recycledBlocks[i] != -1ULL) emptyPage = false;
            }

        } while (!md.refCount.compare_exchange_strong(oldCount, oldCount - 1));

        log(LogAlloc, "p-exit-2", "p=%p, free=%d", this, emptyPage);

        /** If emptyPage is set then all blocks in this page have been freed and
            there will therefor not be any other threads that can increment
            refCount. If refCount has reached 0 then we're the last thread
            within this page and since no other threads can access this page
            then it's safe to delete the page.
         */
        if (oldCount > 1 || !emptyPage) return false;

        alignedFree(this);
        return true;
    }


    /** Indicates that the page will no longer be used for allocation and that
        it should be reclaimed whenever it is safe to do so.
     */
    bool kill()
    {
        enterFree();
        md.refCount &= ~(1ULL << 63);
        return exitFree();
    }


    /** Could be called from multiple threads and should therefor not manipulate
        the freeBlocks bitfield. Instead we only manipulate the recycledBlocks
        bitfield which alloc will apply in batch back into the freeBlocks
        bitfield.

        This scheme has the effect of reducing the amount of synchronization
        between the allocation thread and the deallocation threads.
     */
    bool free(void* ptr)
    {
        locklessCheckGt(ptr, this, log);
        locklessCheckLt(ptr, this + sizeof(*this), log);

        size_t block =
            (uintptr_t(ptr) - uintptr_t(this)) / Policy::BlockSize
            - MetadataBlocks;

        log(LogAlloc, "p-free-0", "p=%p, ptr=%p, block=%ld", this, ptr, block);
        locklessCheckLt(block, NumBlocks, log);

        size_t topIndex = block / 64;
        size_t subIndex = block % 64;

        enterFree();
        md.recycledBlocks[topIndex] |= 1ULL << subIndex;
        return exitFree();
    }

    static BlockPage<Policy>* pageForBlock(void* block)
    {
        return reinterpret_cast<BlockPage<Policy>*>(
                uintptr_t(block) & ~(Policy::PageSize - 1));
    }

    std::string print() const
    {
        std::string freeStr =
            printBitfield(md.freeBlocks, BitfieldSize, -1ULL);

        std::string recycledStr =
            printBitfield(md.recycledBlocks, BitfieldSize, 0);

        return format("ref=%s, next=%p, free=[ %s], rec=[ %s]",
                printRefCount(md.refCount).c_str(),
                md.next, freeStr.c_str(), recycledStr.c_str());
    }

    static typename BlockAllocLog<Policy>::type& log;
};

template<typename Policy>
typename BlockAllocLog<Policy>::type&
BlockPage<Policy>::
log = BlockAllocLog<Policy>::log;


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
        if (!tail) {
            pushFront(page);
            return;
        }

        page->next(nullptr);
        tail->next(page);
        tail = page;
    }

    void dump()
    {
        std::cerr << "[ ";

        Page* page = head;
        while (page) {
            std::cerr << format("%p ", page);
            page = page->next();
        }

        std::cerr << "]" << std::endl;
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

    BlockAllocTls() : nextRecycledPage(nullptr)
    {
        log(LogAlloc, "t-init", "");
    }

    ~BlockAllocTls()
    {
        log(LogAlloc, "t-destruct", "qa=%p, qr=%p",
                allocQueue.peek(), recycledQueue.peek());

        Page* page;

        while ((page = allocQueue.peek())) {
            allocQueue.pop();
            page->kill();
        }

        while ((page = recycledQueue.peek())) {
            recycledQueue.pop();
            page->kill();
        }
    }

    void* allocBlock()
    {
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
        Page* page = prev && prev->next() ? prev->next() : recycledQueue.peek();

        log(LogAlloc, "t-alloc-0", "prev=%p, p=%p", prev, page);

        if (page) {
            if (page->hasFreeBlock()) {
                recycledQueue.remove(page, prev);
                allocQueue.pushBack(page);

                log(LogAlloc, "t-alloc-1", "p=%p -> recycled", page);
            }
            nextRecycledPage = page;
        }


        /** Alrighty, time to allocate a block. */
        page = allocQueue.peek();
        if (page) {
            void* ptr = page->alloc();
            log(LogAlloc, "t-alloc-2", "p=%p, ptr=%p", page, ptr);

            if (!page->hasFreeBlock()) {
                allocQueue.pop();
                recycledQueue.pushBack(page);

                log(LogAlloc, "t-alloc-3", "p=%p -> full", page);
            }

            // Invariant states that allocQueue should either be empty or a
            // block is available in the head.
            locklessCheck(ptr, log);
            return ptr;
        }


        // Couldn't find a block so create create a new page.
        page = details::BlockPage<Policy>::create();
        log(LogAlloc, "t-alloc-4", "p=%p -> new", page);
        if (!page) return nullptr;

        allocQueue.pushFront(page);

        void* ptr = page->alloc();
        locklessCheck(ptr, log);
        log(LogAlloc, "t-alloc-5", "p=%p, ptr=%p", page, ptr);

        return ptr;
    }


    /** Free returns the block to the thread that allocated it which means that
        this function has to be thread-safe. To keep the allocator as simple as
        possible, free can't manipulate the allocation queues which means that
        allocBlock() can is almost entirely single-threaded.
    */
    void freeBlock(void* ptr)
    {
        Page* page = details::BlockPage<Policy>::pageForBlock(ptr);
        log(LogAlloc, "t-free", "page=%p, ptr=%p", page, ptr);

        page->free(ptr);
    }

    static typename BlockAllocLog<Policy>::type& log;
};

template<typename Policy>
typename BlockAllocLog<Policy>::type&
BlockAllocTls<Policy>::
log = BlockAllocLog<Policy>::log;

} // namespace details


// \todo This really really really suck and I'll deal with it later.
#ifdef log
#   undef log
#endif

/******************************************************************************/
/* BLOCK ALLOC                                                                */
/******************************************************************************/

template<typename Policy, typename Tag>
LogAggregator
BlockAlloc<Policy, Tag>::
log()
{
    return LogAggregator(details::BlockAllocLog<Policy>::log);
}


template<typename Policy, typename Tag>
Tls<details::BlockAllocTls<Policy>, Tag>
BlockAlloc<Policy, Tag>::
allocator;


} // lockless
