/* alloc_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 18 Jun 2013
   FreeBSD-style copyright and disclaimer apply

   tests for the block allocator.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "alloc.h"
#include "check.h"
#include "debug.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <set>
#include <array>
#include <algorithm>


using namespace std;
using namespace lockless;
using namespace lockless::details;


/******************************************************************************/
/* UTILS                                                                      */
/******************************************************************************/

#define SIZE_SEQ                                                        \
    (0x01) (0x02) (0x03) (0x04) (0x05) (0x06) (0x07) (0x08) (0x0C)      \
    (0x10) (0x11) (0x18) (0x20) (0x30) (0x40) (0x80) (0x8F) (0xCF)      \
    (0x0100) (0x0111) (0x0200) (0x0400) (0x0800) (0x1000) (0x1111)      \
    (0x00010000) (0x00100000) (0x01000000) (0x01111111) (0xFFFFFFFF)

void checkPow2(size_t val)
{
    locklessCheckEq(val & (val - 1), 0ULL, NullLog);
}

void checkAlign(size_t val, size_t align)
{
    checkPow2(align);
    locklessCheckEq(val & (align - 1), 0ULL, NullLog);
}

template<typename Policy>
void fillBlock(void* block, size_t value = 0ULL)
{
    memset(block, Policy::BlockSize, value);
}

template<typename Policy>
void checkBlock(void* block, size_t value = 0ULL)
{
    uint8_t* pBlock = reinterpret_cast<uint8_t*>(block);
    auto pred = [=] (uint8_t val) { return val == value; };
    locklessCheck(all_of(pBlock, pBlock + Policy::BlockSize, pred), NullLog);
}


/******************************************************************************/
/* POLICY TEST                                                                */
/******************************************************************************/

template<typename Policy>
void checkPolicy(const std::function<void (size_t, size_t)>& fn)
{
    // cerr << fmtTitle(
    //         "blockSize=" + to_string(Policy::BlockSize)
    //         + ", pageSize=" + to_string(Policy::PageSize))
    //     << endl;

    fn(Policy::BlockSize, Policy::PageSize);
}

BOOST_AUTO_TEST_CASE(policyTest)
{

    cerr << fmtTitle("PackedAllocPolicy", '=') << endl;

    auto checkFn = [] (size_t blockSize, size_t pageSize) {
        checkPow2(pageSize);
        locklessCheckGe(pageSize, 4096ULL, NullLog);

        if (blockSize)
            locklessCheckGt(pageSize / blockSize, 64ULL, NullLog);
    };

#define PackedAllocPolicyTest(_r_, _data_, _elem_)      \
    checkPolicy< PackedAllocPolicy<_elem_> >(checkFn);

    BOOST_PP_SEQ_FOR_EACH(PackedAllocPolicyTest, _, SIZE_SEQ);

#undef PackedAllocPolicyTest


    cerr << fmtTitle("AlignedAllocPolicy", '=') << endl;

    auto checkAlignFn = [&] (size_t blockSize, size_t pageSize) {
        checkFn(blockSize, pageSize);
        checkAlign(blockSize, 8);
    };


#define AlignedAllocPolicyTest(_r_, _data_, _elem_)             \
    checkPolicy< AlignedAllocPolicy<_elem_> >(checkAlignFn);

    BOOST_PP_SEQ_FOR_EACH(AlignedAllocPolicyTest, _, SIZE_SEQ);

#undef PackedAllocPolicyTest
}


/******************************************************************************/
/* PAGE TEST                                                                  */
/******************************************************************************/

template<typename Policy>
BlockPage<Policy>* createPage()
{
    typedef BlockPage<Policy> Page;

    Page* page = Page::create();

    checkAlign(uintptr_t(page), Policy::PageSize);
    locklessCheck(!page->next(), NullLog);
    locklessCheck(page->hasFreeBlock(), NullLog);

    return page;
}


template<typename Policy>
void checkPageMd()
{
    typedef BlockPage<Policy> Page;

    checkPow2(sizeof(Page));
    locklessCheckEq(sizeof(Page), Policy::PageSize, NullLog);
    locklessCheckGe(Page::BitfieldSize * 64, Page::NumBlocks, NullLog);
    locklessCheckGe(Page::TotalBlocks, 64ULL, NullLog);
}


template<typename Policy>
void checkPageKill()
{
    typedef BlockPage<Policy> Page;
    array<void*, Page::NumBlocks> blocks;

    locklessCheck(createPage<Policy>()->kill(), NullLog);

    {
        Page* page = createPage<Policy>();

        for (size_t i = 0; i < Page::NumBlocks; ++i)
            blocks[i] = page->alloc();

        locklessCheck(!page->kill(), NullLog);

        for (size_t i = 0; i < Page::NumBlocks; ++i)
            locklessCheckEq(
                    page->free(blocks[i]), i == Page::NumBlocks - 1, NullLog);
    }

    {
        Page* page = createPage<Policy>();

        for (size_t i = 0; i < Page::NumBlocks; ++i)
            blocks[i] = page->alloc();

        for (size_t i = 0; i < Page::NumBlocks / 2; ++i)
            locklessCheckEq(
                    page->free(blocks[i]), i == Page::NumBlocks - 1, NullLog);

        locklessCheck(!page->kill(), NullLog);

        for (size_t i = Page::NumBlocks / 2; i < Page::NumBlocks; ++i)
            locklessCheckEq(
                    page->free(blocks[i]), i == Page::NumBlocks - 1, NullLog);
    }

    {
        Page* page = createPage<Policy>();

        for (size_t i = 0; i < Page::NumBlocks; ++i)
            blocks[i] = page->alloc();

        for (size_t i = 0; i < Page::NumBlocks; ++i)
            locklessCheck(!page->free(blocks[i]), NullLog);

        locklessCheck(page->kill(), NullLog);
    }
}


template<typename Policy>
void checkPageAlloc()
{
    typedef BlockPage<Policy> Page;

    Page* page = createPage<Policy>();

    array<void*, Page::NumBlocks> blocks;
    for (size_t iteration = 0; iteration < 5; ++iteration) {
        for (size_t i = 0; i < Page::NumBlocks; ++i) {
            locklessCheck(page->hasFreeBlock(), NullLog);

            blocks[i] = page->alloc();
            fillBlock<Policy>(blocks[i], i);

            if (!i) {
                locklessCheckEq(
                        page + (Page::MetadataBlocks * Policy::BlockSize),
                        blocks[0],
                        NullLog);
            }
            else {
                locklessCheckGt(blocks[i], page, NullLog);
                locklessCheckLe(
                        static_cast<uint8_t*>(blocks[i]) + Policy::BlockSize,
                        reinterpret_cast<uint8_t*>(page + Policy::PageSize),
                        NullLog);

                locklessCheckEq(
                        static_cast<uint8_t*>(blocks[i-1]) + Policy::BlockSize,
                        blocks[i],
                        NullLog);
            }

            locklessCheckEq(page, Page::pageForBlock(blocks[i]), NullLog);
            page->free(blocks[i]);
        }
    }

    locklessCheck(page->kill(), NullLog);
}


template<typename Policy>
void checkPageFull()
{
    typedef BlockPage<Policy> Page;

    Page* page = createPage<Policy>();

    array<void*, Page::NumBlocks> blocks;
    for (size_t i = 0; i < Page::NumBlocks; ++i)
        page->free(blocks[i] = page->alloc());

    for (size_t k = 1; k < Page::NumBlocks; ++k) {
        for (size_t i = 0; i < Page::NumBlocks; ++i)
            locklessCheckEq(page->alloc(), blocks[i], NullLog);

        locklessCheck(!page->hasFreeBlock(), NullLog);

        for (size_t i = 0; i < Page::NumBlocks; i += k)
            page->free(blocks[i]);

        for (size_t i = 0; i < Page::NumBlocks; i += k)
            locklessCheckEq(page->alloc(), blocks[i], NullLog);

        for (size_t i = 0; i < Page::NumBlocks; ++i)
            page->free(blocks[i]);
    }

    locklessCheck(page->kill(), NullLog);
}


template<typename Policy>
void checkPageRandom()
{
    typedef BlockPage<Policy> Page;

    static mt19937_64 rng;
    uniform_int_distribution<size_t> actionRnd(0, 10);

    set<Page*> active;
    set<Page*> killed;
    set<void*> allocated;

    auto hasBlocks = [&] (Page* page) {
        return distance(
                allocated.lower_bound(page),
                allocated.upper_bound(page + Policy::PageSize));
    };

    auto rndPage = [&] (const set<Page*>& s) {
        uniform_int_distribution<size_t> pageRnd(0, s.size() - 1);
        auto it = s.begin();
        advance(it, pageRnd(rng));
        return it;
    };

    auto rndBlock = [&] {
        uniform_int_distribution<size_t> blockRnd(0, allocated.size() - 1);
        auto it = allocated.begin();
        advance(it, blockRnd(rng));
        return it;
    };

    auto killPage = [&] (Page* page) {
        locklessCheck(active.erase(page), NullLog);

        if (hasBlocks(page)) {
            locklessCheck(!page->kill(), NullLog);
            locklessCheck(killed.insert(page).second, NullLog);
        }
        else locklessCheck(page->kill(), NullLog);
    };

    auto freeBlock = [&] (void* block) {
        checkBlock<Policy>(block, 1ULL);
        locklessCheck(allocated.erase(block), NullLog);

        Page* page = Page::pageForBlock(block);

        if (active.count(page))
            locklessCheck(!page->free(block), NullLog);

        else if (killed.count(page)) {
            if (!hasBlocks(page)) {
                locklessCheck(page->free(block), NullLog);
                killed.erase(page);
            }
            else locklessCheck(!page->free(block), NullLog);
        }

        else locklessCheck(false, NullLog);
    };

    for (size_t iterations = 0; iterations < 1000; ++iterations) {
        unsigned action = actionRnd(rng);

        if ((active.empty() && killed.empty()) || action < 1)
            locklessCheck(active.insert(createPage<Policy>()).second, NullLog);

        else if (!active.empty() && action < 2)
            killPage(*rndPage(active));

        else if ((allocated.empty() && !active.empty()) || action < 7) {
            auto pageIt = rndPage(active);
            if (!(*pageIt)->hasFreeBlock()) continue;

            void* block = (*pageIt)->alloc();
            fillBlock<Policy>(block, 1ULL);
            locklessCheck(allocated.insert(block).second, NullLog);
        }

        else if (!allocated.empty())
            freeBlock(*rndBlock());
    }

    while (!killed.empty()) killPage(*active.begin());
    while (!allocated.empty()) freeBlock(*allocated.begin());
}


template<typename Policy>
void checkPage()
{
    checkPageMd<Policy>();

    if (!Policy::BlockSize) return;

    checkPageKill<Policy>();
    checkPageAlloc<Policy>();
    checkPageFull<Policy>();
    checkPageRandom<Policy>();
}

BOOST_AUTO_TEST_CASE(pageTest)
{

#define PackedPageTest(_r_, _data_, _elem_)    \
    checkPage< PackedAllocPolicy<_elem_> >();

    BOOST_PP_SEQ_FOR_EACH(PackedPageTest, _, SIZE_SEQ);

#undef PackedPageTest


#define AlignedPageTest(_r_, _data_, _elem_)    \
    checkPage< AlignedAllocPolicy<_elem_> >();

    BOOST_PP_SEQ_FOR_EACH(AlignedPageTest, _, SIZE_SEQ);

#undef AlignedPageTest

}
