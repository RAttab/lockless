/* arena_para_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 24 Aug 2013
   FreeBSD-style copyright and disclaimer apply

   Parallel tests for the arena allocator
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
#include <array>
#include <random>
#include <unordered_map>
#include <unordered_set>

using namespace std;
using namespace lockless;


uint8_t firstChar(uintptr_t ptr)
{
    return ((uint8_t*)ptr)[0];
}

BOOST_AUTO_TEST_CASE(simpleTest)
{
    enum {
        Threads = 8,
        Iterations = 100000
    };

    Arena<PageSize> arena;
    auto& log = arena.log;

    array<unordered_map<uintptr_t, size_t>, Threads> seen;

    auto allocThread = [&] (unsigned id) {
        mt19937_64 rng;
        uniform_int_distribution<unsigned> dist(1, 67);

        for (size_t it = 0; it < Iterations; ++it) {
            size_t size = dist(rng);
            uintptr_t ptr = (uintptr_t) arena.alloc(size);
            locklessCheck(seen[id].insert(make_pair(ptr, size)).second, log);
            memset((void*) ptr, id * it, size);
        }
    };

    ParallelTest test;
    test.add(allocThread, Threads);
    test.run();

    unordered_set<uintptr_t> total;
    for (auto& s : seen) {
        for (auto& entry : s) {
            uintptr_t ptr = entry.first;
            size_t size = entry.second;

            locklessCheck(total.insert(ptr).second, log);
            checkMem((void*)ptr, size, firstChar(ptr), log, locklessCtx());
        }
    }
}


BOOST_AUTO_TEST_CASE(alignTest)
{
    enum {
        Threads = 8,
        Iterations = 100000
    };

    Arena<PageSize> arena;
    auto& log = arena.log;

    array<unordered_map<uintptr_t, size_t>, Threads> seen;

    auto allocThread = [&] (unsigned id) {
        mt19937_64 rng;
        uniform_int_distribution<unsigned> sizeDist(1, 67);
        uniform_int_distribution<unsigned> alignDist(1, 5);

        for (size_t it = 0; it < Iterations; ++it) {
            size_t size = sizeDist(rng);
            size_t align = 1ULL << alignDist(rng);
            uintptr_t ptr = (uintptr_t) arena.alloc(size, align);
            checkAlign(ptr, align, log, locklessCtx());
            locklessCheck(seen[id].insert(make_pair(ptr, size)).second, log);
            memset((void*) ptr, id * it, size);
        }
    };

    ParallelTest test;
    test.add(allocThread, Threads);
    test.run();

    unordered_set<uintptr_t> total;
    for (auto& s : seen) {
        for (auto& entry : s) {
            uintptr_t ptr = entry.first;
            size_t size = entry.second;

            locklessCheck(total.insert(ptr).second, log);
            checkMem((void*)ptr, size, firstChar(ptr), log, locklessCtx());
        }
    }
}
