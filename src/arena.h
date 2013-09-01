/* arena.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 24 Aug 2013
   FreeBSD-style copyright and disclaimer apply

   Arena allocator

   \todo For this to have any chance against tcmalloc we need to make it
   thread-local. This has unfortunate problem that we need an instanced TLS
   which is all kinds of tricky to get right and probably blow our entire time
   budget.

 */

#ifndef __lockless__arena_h__
#define __lockless__arena_h__

#include "check.h"
#include "log.h"

#include <atomic>
#include <memory>

namespace lockless {

/******************************************************************************/
/* ARENA PAGE                                                                 */
/******************************************************************************/

namespace details {

template<size_t Size>
struct ArenaPage
{
    typedef ArenaPage<Size> Page;

    ArenaPage(Page* page) : top(0), prev(page) {}

    void* alloc(size_t size)
    {
        size_t index = top.fetch_add(size);
        if (index + size >= data.size()) return nullptr;
        return &data[index];
    }

    void* alloc(size_t size, size_t align)
    {
        size_t index;
        size_t alignedSize;

        do {
            index = top;
            size_t start = reinterpret_cast<size_t>(data.data());
            size_t extra = (start + index) & (align - 1);
            alignedSize = size + (extra ? align - extra : 0);
        } while(!top.compare_exchange_weak(index, index + alignedSize));

        if ((index + alignedSize) >= data.size()) return nullptr;
        return &data[index + alignedSize - size];
    }

    // If Size is page aligned then we don't want to allocate an entire page to
    // hold 128 bits... It's a bit wastefull.
    locklessEnum size_t AdjSize = Size - (sizeof(size_t) + sizeof(Page*));

    std::array<uint8_t, AdjSize> data;
    std::atomic<size_t> top;
    Page* prev;
};

} // namespace details


/******************************************************************************/
/* ARENA                                                                      */
/******************************************************************************/

template<size_t Size>
struct Arena
{
    typedef details::ArenaPage<Size> Page;

    Arena() : head(nullptr) {}
    ~Arena() { clear(); }

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    Arena(Arena&& other) : head(other.head.exchange(nullptr)) {}
    Arena& operator=(Arena&& other)
    {
        if (&other == this) return *this;
        head = other.head.exhange(nullptr);
        return *this;
    }


    template<typename T, typename... Args>
    T* allocT(Args&&... args)
    {
        void* ptr = alloc(sizeof(T));
        return new (ptr) T(std::forward<Args>(args)...);
    }


    void* alloc(size_t size, size_t align = 0)
    {
        locklessCheck(!align || !(align & (align - 1)), log);
        locklessCheckLe(size, Size, log);
        log(LogAlloc, "alloc-0", "size=%lld, align=%lld", size, align);

        Page* page = head;

        for (size_t attempt = 0; attempt < 10; ++attempt) {

            if (page) {
                void* ptr = align ? page->alloc(size, align) : page->alloc(size);
                log(LogAlloc, "alloc-1", "page=%p, ptr=%p", page, ptr);
                if (ptr) return ptr;
            }

            page = addPage(page);
        }

        log(LogAlloc, "alloc-2", "page=%p", page);
        return nullptr;
    }


    /** Not safe to call with concurrent calls to alloc. Concurrent calls to
        clear() are safe.
     */
    void clear()
    {
        Page* page = head.exchange(nullptr);
        log(LogAlloc, "wipe-0", "page=%p", page);

        while(page) {
            Page* prev = page->prev;
            log(LogAlloc, "wipe-1", "page=%p, prev=%p", page, prev);

            delete page;
            page = prev;
        }
    }

private:

    Page* addPage(Page* old)
    {
        std::unique_ptr<Page> page(new Page(old));
        log(LogAlloc, "add", "page=%p, new=%p", old, page.get());
        return head.compare_exchange_strong(old, page.get()) ?
            page.release() : old;
    }

    std::atomic<Page*> head;

public:
    DebuggingLog<1000, DebugAlloc>::type log;
};


} // lockless

#endif // __lockless__arena_h__
