/* alloc_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 18 Jun 2013
   FreeBSD-style copyright and disclaimer apply

   tests for the block allocator.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#define LOCKLESS_ALLOC_DEBUG 1
#define LOCKLESS_CHECK_ABORT 1

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

// Adding anything to this increases the compilation time by a ridiculous amount
#define SIZE_SEQ                                \
    (0x01) (0x02) (0x03) (0x04) (0x06) (0x08)   \
    (0x10) (0x1F) (0x30) (0x8F) (0xFFFF)


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
void fillBlock(void* block, uint8_t value = 0)
{
    memset(block, value, Policy::BlockSize);
}

template<typename Policy>
void checkBlock(void* block, uint8_t value = 0)
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

    cerr << fmtTitle("policy - packed", '=') << endl;

    auto checkFn = [] (size_t blockSize, size_t pageSize) {
        checkPow2(pageSize);
        locklessCheckGe(pageSize, 4096ULL, NullLog);

        if (blockSize)
            locklessCheckGe(pageSize / blockSize, 64ULL, NullLog);
    };

#define PackedAllocPolicyTest(_r_, _data_, _elem_)      \
    checkPolicy< PackedAllocPolicy<_elem_> >(checkFn);

    BOOST_PP_SEQ_FOR_EACH(PackedAllocPolicyTest, _, SIZE_SEQ);

#undef PackedAllocPolicyTest


    cerr << fmtTitle("policy - aligned", '=') << endl;

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

    locklessCheckEq(uintptr_t(page), uintptr_t(&page->md), NullLog);
    locklessCheckGe(
            uintptr_t(&page->blocks),
            uintptr_t(&page->md) + sizeof(typename Page::Metadata),
            NullLog);

    locklessCheckEq(
            uintptr_t(page) + (Page::MetadataBlocks * Policy::BlockSize),
            uintptr_t(&page->blocks),
            NullLog);

    return page;
}

template<typename Policy>
void printPageMd()
{
    typedef BlockPage<Policy> Page;

    cerr << fmtTitle(
            "block=" + to_string(Policy::BlockSize) +
            ", page=" + to_string(Policy::PageSize))
        << endl;

    typedef CalcPageSize<Policy::BlockSize, 64> Calc;

    size_t sizeofMd = sizeof(Page::md);
    size_t sizeofPadding = sizeofMd - sizeof(typename Page::Metadata);

    cerr << "\ttotal      " << Page::TotalBlocks << endl
        << "\tbfEstimate " << Page::BitfieldEstimate << endl
        << "\tmdSize     " << sizeofMd << endl
        << "\tpadSize    " << sizeofPadding << endl
        << "\tmdBlocks   " << Page::MetadataBlocks << endl
        << "\tnumBlocks  " << Page::NumBlocks << endl
        << "\tbfSize     " << Page::BitfieldSize << endl
        << endl
        << "\tsizeofBlck " << sizeof(Page::blocks) << endl
        << "\tsizeofMd   " << sizeofMd << endl
        << "\t           " << sizeof(Page::md) << endl
        << "\t           " << (Page::MetadataBlocks * Policy::BlockSize) << endl
        << "\tsizeofPage " << sizeof(Page) << endl
        << "\t           " << (sizeof(Page::md) + sizeof(Page::blocks)) << endl
        << "\tassert     "
        << (sizeof(Page::md) + sizeof(Page::blocks) <= Policy::PageSize) << endl
        << endl
        << "\tmd.free    " << sizeof(Page::Metadata::freeBlocks) << endl
        << "\tmd.rec     " << sizeof(Page::Metadata::recycledBlocks) << endl
        << "\tmd.ref     " << sizeof(Page::Metadata::refCount) << endl
        << "\tmd.next    " << sizeof(Page::Metadata::next) << endl
        << endl;
}


template<typename Policy>
void checkPageMd()
{
    cerr << fmtTitle("md", '.') << endl;

    typedef BlockPage<Policy> Page;

    locklessCheckLe(sizeof(Page), Policy::PageSize, NullLog);
    locklessCheckGe(Page::BitfieldSize * 64, Page::NumBlocks, NullLog);
    locklessCheckGe(Page::TotalBlocks, 64ULL, NullLog);
}


template<typename Policy>
void checkPageKill()
{
    cerr << fmtTitle("kill", '.') << endl;

    typedef BlockPage<Policy> Page;
    array<void*, Page::NumBlocks> blocks;

    auto& log = Page::log;

    locklessCheck(createPage<Policy>()->kill(), log);

    {
        Page* page = createPage<Policy>();

        for (size_t i = 0; i < Page::NumBlocks; ++i)
            blocks[i] = page->alloc();

        locklessCheck(!page->kill(), log);

        for (size_t i = 0; i < Page::NumBlocks; ++i)
            locklessCheckEq(
                    page->free(blocks[i]), i == Page::NumBlocks - 1, log);
    }

    {
        Page* page = createPage<Policy>();

        for (size_t i = 0; i < Page::NumBlocks; ++i)
            blocks[i] = page->alloc();

        for (size_t i = 0; i < Page::NumBlocks / 2; ++i)
            locklessCheckEq(
                    page->free(blocks[i]), i == Page::NumBlocks - 1, log);

        locklessCheck(!page->kill(), log);

        for (size_t i = Page::NumBlocks / 2; i < Page::NumBlocks; ++i)
            locklessCheckEq(
                    page->free(blocks[i]), i == Page::NumBlocks - 1, log);
    }

    {
        Page* page = createPage<Policy>();

        for (size_t i = 0; i < Page::NumBlocks; ++i)
            blocks[i] = page->alloc();

        for (size_t i = 0; i < Page::NumBlocks; ++i)
            locklessCheck(!page->free(blocks[i]), log);

        locklessCheck(page->kill(), log);
    }
}


template<typename Policy>
void checkPageAlloc()
{
    cerr << fmtTitle("alloc", '.') << endl;

    typedef BlockPage<Policy> Page;

    Page* page = createPage<Policy>();
    auto& log = Page::log;

    array<void*, Page::NumBlocks> blocks;

    for (size_t i = 0; i < Page::NumBlocks; ++i) {
        locklessCheck(page->hasFreeBlock(), log);

        void* block = page->alloc();
        fillBlock<Policy>(block, i);

        page->free(blocks[i] = block);

        if (!i) {
            locklessCheckEq(
                    uintptr_t(page) + (Page::MetadataBlocks * Policy::BlockSize),
                    uintptr_t(blocks[0]),
                    log);
        }
        else {
            locklessCheckGt(blocks[i], page, log);
            locklessCheckLe(
                    uintptr_t(blocks[i]) + Policy::BlockSize,
                    uintptr_t(page) + Policy::PageSize,
                    log);

            locklessCheckEq(
                    uintptr_t(blocks[i-1]) + Policy::BlockSize,
                    uintptr_t(blocks[i]),
                    log);
        }

        locklessCheckEq(page, Page::pageForBlock(blocks[i]), log);
    }

    for (size_t iteration = 0; iteration < 2; ++iteration) {
        for (size_t i = 0; i < Page::NumBlocks; ++i)
            locklessCheckEq(blocks[i], page->alloc(), log);

        for (size_t i = 0; i < Page::NumBlocks; ++i)
            page->free(blocks[i]);
    }

    locklessCheck(page->kill(), log);
}


template<typename Policy>
void checkPageFull()
{
    cerr << fmtTitle("full", '.') << endl;

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
    cerr << fmtTitle("random", '.') << endl;

    typedef BlockPage<Policy> Page;
    auto& log = Page::log;

    static mt19937_64 rng;
    uniform_int_distribution<size_t> actionRnd(0, 10);

    set<Page*> active;
    set<Page*> killed;
    set<void*> allocated;

    auto hasBlocks = [&] (Page* page) {
        return distance(
                allocated.lower_bound(page),
                allocated.upper_bound(page + 1));
    };

    auto rndPage = [&] (const set<Page*>& s) {
        uniform_int_distribution<size_t> pageRnd(0, s.size() - 1);
        auto it = s.begin();
        advance(it,pageRnd(rng));
        return it;
    };

    auto rndBlock = [&] {
        uniform_int_distribution<size_t> blockRnd(0, allocated.size() - 1);
        auto it = allocated.begin();
        advance(it, blockRnd(rng));
        return it;
    };

    auto killPage = [&] (Page* page) {
        locklessCheck(active.erase(page), log);

        if (hasBlocks(page)) {
            locklessCheck(!page->kill(), log);
            locklessCheck(killed.insert(page).second, log);
        }
        else locklessCheck(page->kill(), log);
    };

    auto freeBlock = [&] (void* block) {
        checkBlock<Policy>(block, 1ULL);
        locklessCheck(allocated.erase(block), log);

        Page* page = Page::pageForBlock(block);

        if (active.count(page))
            locklessCheck(!page->free(block), log);

        else if (killed.count(page)) {
            if (!hasBlocks(page)) {
                locklessCheck(page->free(block), log);
                killed.erase(page);
            }
            else locklessCheck(!page->free(block), log);
        }

        else locklessCheck(false, log);
    };

    for (size_t iterations = 0; iterations < 100; ++iterations) {
        unsigned action = actionRnd(rng);

        if ((active.empty() && killed.empty()) || action < 1)
            locklessCheck(active.insert(createPage<Policy>()).second, log);

        else if (!active.empty() && action < 2)
            killPage(*rndPage(active));

        else if ((allocated.empty() && !active.empty()) || action < 7) {
            if (active.empty()) { --iterations; continue; }

            auto pageIt = rndPage(active);
            if (!(*pageIt)->hasFreeBlock()) { --iterations; continue; }

            void* block = (*pageIt)->alloc();
            fillBlock<Policy>(block, 1ULL);
            locklessCheck(allocated.insert(block).second, log);
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

    printPageMd<Policy>();
    checkPageMd<Policy>();

    if (!Policy::BlockSize) return;

    checkPageKill<Policy>();
    checkPageAlloc<Policy>();
    checkPageFull<Policy>();
    checkPageRandom<Policy>();
}

BOOST_AUTO_TEST_CASE(pageTest)
{

    cerr << fmtTitle("page - packed", '=') << endl;

#define PackedPageTest(_r_, _data_, _elem_)    \
    checkPage< PackedAllocPolicy<_elem_> >();

    BOOST_PP_SEQ_FOR_EACH(PackedPageTest, _, SIZE_SEQ);

#undef PackedPageTest

    cerr << fmtTitle("page - aligned", '=') << endl;

#define AlignedPageTest(_r_, _data_, _elem_)    \
    checkPage< AlignedAllocPolicy<_elem_> >();

    BOOST_PP_SEQ_FOR_EACH(AlignedPageTest, _, SIZE_SEQ);

#undef AlignedPageTest

}
