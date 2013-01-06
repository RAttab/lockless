/* rcu_para_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 05 Jan 2013
   FreeBSD-style copyright and disclaimer apply

   Parallel tests for rcu.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#define LOCKLESS_RCU_DEBUG 1
#define LOCKLESS_CHECK_ABORT 1

#include "rcu.h"
#include "check.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>
#include <iostream>
#include <random>
#include <map>
#include <memory>
#include <thread>

using namespace std;
using namespace lockless;


BOOST_AUTO_TEST_CASE(simpleTest)
{
    enum {
        Threads = 8,
        Iterations = 10000,
    };

    array<atomic<size_t>, Threads> counters;
    for (auto& c : counters) c.store(0);

    {
        Rcu rcu;

        auto doThread = [&] (unsigned id) {
            for (size_t i = 0; i < Iterations; ++i) {
                RcuGuard guard(rcu);
                rcu.defer([&, id] { counters[id]++; });
            }
        };

        ParallelTest test;
        test.add(doThread, Threads);
        test.run();
    }

    for (size_t i = 0; i < counters.size(); ++i)
        locklessCheckEq(counters[i], Iterations);
}


BOOST_AUTO_TEST_CASE(complexTest)
{
    enum {
        ReadThreads = 2,
        WriteThreads = 2,
        Iterations = 10000,
        Slots = 100,
    };

    const uint64_t MAGIC_VALUE = 0xDEADBEEFDEADBEEFULL;

    struct Obj
    {
        uint64_t value;

        Obj() : value(MAGIC_VALUE) {}
        ~Obj() { check(); value = 0; }
        void check() const { locklessCheckEq(value, MAGIC_VALUE); }
    };

    Rcu rcu;
    atomic<unsigned> doneCount(0);

    array< atomic<Obj*>, Slots> slots;
    for (auto& obj : slots) obj.store(nullptr);

    auto doWriteThread = [&] (unsigned) {
        for (size_t it = 0; it < Iterations; ++it) {
            RcuGuard guard(rcu);

            for (size_t index = Slots; index > 0; --index) {
                Obj* obj = slots[index - 1].exchange(new Obj());
                if (obj) rcu.defer([=] { obj->check(); delete obj; });
            }
        }

        doneCount++;
    };

    auto doReadThread = [&] (unsigned) {
        do {
            RcuGuard guard(rcu);

            for (size_t index = 0; index < Slots; ++index) {
                Obj* obj = slots[index].load();
                if (obj) obj->check();
            }

        } while (doneCount.load() < WriteThreads);
    };

    ParallelTest test;
    test.add(doWriteThread, WriteThreads);
    test.add(doReadThread, ReadThreads);
    test.run();

    for (auto& obj : slots) {
        if (!obj.load()) delete obj.load();
    }
}
