/* arena_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 24 Aug 2013
   FreeBSD-style copyright and disclaimer apply

   Tests for the arena allocator
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#define LOCKLESS_ALLOC_DEBUG 1
#define LOCKLESS_CHECK_ABORT 1

#include "arena.h"
#include "check.h"
#include "debug.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>
#include <unordered_set>
#include <unordered_map>

using namespace std;
using namespace lockless;

uint8_t firstChar(uintptr_t ptr)
{
    return ((uint8_t*)ptr)[0];
}

BOOST_AUTO_TEST_CASE(allocTest)
{
    enum {
        Iterations = 100,
        Blocks = 2000,
        Size = 8,
    };

    Arena<PageSize> arena;
    auto& log = arena.log;

    for (size_t it = 0; it < Iterations; ++it) {
        unordered_set<uintptr_t> seen;

        for (size_t i = 0; i < Blocks; ++i) {
            uintptr_t ptr = (uintptr_t) arena.alloc(Size);
            locklessCheck(seen.insert(ptr).second, log);
            memset((void*)ptr, i, Size);
        }

        for (uintptr_t ptr : seen)
            checkMem((void*)ptr, Size, firstChar(ptr), log, locklessCtx());

        arena.clear();
    }
}

BOOST_AUTO_TEST_CASE(alignTest)
{
    enum {
        Iterations = 1000,
        MaxSize = 31,
        MaxAlign = 16
    };

    Arena<PageSize> arena;
    auto& log = arena.log;

    unordered_map<uintptr_t, size_t> seen;

    for (size_t it = 0; it < Iterations; ++it) {
        for (size_t align = 1; align <= MaxAlign; align *= 2) {
            for (size_t size = 1; size <= MaxSize; ++size) {
                uintptr_t ptr = (uintptr_t)arena.alloc(size, align);
                locklessCheck(seen.insert(make_pair(ptr, size)).second, log);
                checkAlign(ptr, align, log, locklessCtx());
                memset((void*)ptr, it + align + size, size);
            }
        }
    }

    for (const auto& entry : seen) {
        uintptr_t ptr = entry.first;
        size_t size = entry.second;
        checkMem((void*)ptr, size, firstChar(ptr), log, locklessCtx());
    }
}
