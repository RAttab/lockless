/* arena_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 24 Aug 2013
   FreeBSD-style copyright and disclaimer apply

   Tests for the arena allocator
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#define LOCKLESS_ALLOC_DEBUG 1
#define LOCKLESS_CHECK_ABORT 1

#include <iostream>

#include "arena.h"
#include "check.h"
#include "debug.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>
#include <unordered_set>

using namespace std;
using namespace lockless;

BOOST_AUTO_TEST_CASE(allocTest)
{
    enum { Blocks = 2000 };

    Arena<PageSize> arena;
    auto& log = arena.log;

    for (size_t attempt = 0; attempt < 5; ++attempt) {
        unordered_set<uintptr_t> seen;

        for (size_t i = 0; i < Blocks; ++i) {
            uintptr_t ptr = (uintptr_t) arena.alloc(8);
            locklessCheck(seen.insert(ptr).second, log);
        }

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

    unordered_set<uintptr_t> seen;

    for (size_t it = 0; it < Iterations; ++it) {
        for (size_t align = 1; align <= MaxAlign; align *= 2) {
            for (size_t size = 1; size <= MaxSize; ++size) {
                uintptr_t ptr = (uintptr_t)arena.alloc(size, align);
                locklessCheck(seen.insert(ptr).second, log);
                locklessCheckEq(ptr, ptr & ~(align - 1), log);
            }
        }
    }
}
