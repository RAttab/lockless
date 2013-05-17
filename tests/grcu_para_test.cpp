/* grcu_para_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 16 May 2013
   FreeBSD-style copyright and disclaimer apply

   Parallel tests for the global rcu implementation.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#define LOCKLESS_RCU_DEBUG 1
#define LOCKLESS_CHECK_ABORT 1

#include "grcu.h"
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
    cerr << fmtTitle("simpleTest", '=') << endl;

    enum {
        Threads = 8,
        Iterations = 1000000,
    };

    array<atomic<size_t>, Threads> counters;
    for (auto& c : counters) c.store(0);

    {
        auto doThread = [&] (unsigned id) {
            GlobalRcu rcu;
            for (size_t i = 0; i < Iterations; ++i) {
                RcuGuard<GlobalRcu> guard(rcu);
                rcu.defer([&, id] { counters[id]++; });
            }
        };

        GcThread gcThread;

        ParallelTest test;
        test.add(doThread, Threads);
        test.run();

        gcThread.join();
    }

    for (size_t i = 0; i < counters.size(); ++i)
        locklessCheckEq(counters[i], Iterations, NullLog);
}


BOOST_AUTO_TEST_CASE(complexTest)
{
    cerr << fmtTitle("complexTest", '=') << endl;

    enum {
        ReadThreads = 4,
        WriteThreads = 4,
        Iterations = 10000,
        Slots = 100,
    };

    const uint64_t MAGIC_VALUE = 0xDEADBEEFDEADBEEFULL;

    struct Obj
    {
        uint64_t value;

        Obj() : value(MAGIC_VALUE) {}
        ~Obj() { check(); value = 0; }
        void check() const { locklessCheckEq(value, MAGIC_VALUE, NullLog); }
    };

    atomic<unsigned> doneCount(0);

    array< atomic<Obj*>, Slots> slots;
    for (auto& obj : slots) obj.store(nullptr);

    auto doWriteThread = [&] (unsigned) {
        GlobalRcu rcu;

        for (size_t it = 0; it < Iterations; ++it) {
            RcuGuard<GlobalRcu> guard(rcu);

            for (size_t index = Slots; index > 0; --index) {
                Obj* obj = slots[index - 1].exchange(new Obj());
                if (obj) rcu.defer([=] { obj->check(); delete obj; });
            }
        }

        doneCount++;
    };

    auto doReadThread = [&] (unsigned) {
        GlobalRcu rcu;
        do {
            RcuGuard<GlobalRcu> guard(rcu);

            for (size_t index = 0; index < Slots; ++index) {
                Obj* obj = slots[index].load();
                if (obj) obj->check();
            }

        } while (doneCount.load() < WriteThreads);
    };

    GcThread gcThread;

    ParallelTest test;
    test.add(doWriteThread, WriteThreads);
    test.add(doReadThread, ReadThreads);
    test.run();

    gcThread.join();

    for (auto& obj : slots) {
        if (!obj.load()) delete obj.load();
    }
}


BOOST_AUTO_TEST_CASE(threadCreationTest)
{
    cerr << fmtTitle("threadCreationTest", '=') << endl;

    enum {
        CreatorThreads = 8,
        Threads = 100,
        Iterations = 100,
    };

    array<atomic<size_t>, Threads> counters;
    for (auto& c: counters) c = 0;

    auto doThread = [&] {
        GlobalRcu rcu;
        for (size_t i = 0; i < Iterations; ++i) {
            RcuGuard<GlobalRcu> guard(rcu);
            rcu.defer([&, i] { counters[i]++; });
        }
    };

    auto doCreatorThread = [&] (unsigned) {
        array<unique_ptr<thread>, Threads> threads;

        for (auto& th: threads) th.reset(new thread(doThread));

        for (auto& th: threads) {
            th->join();
            th.reset();
        }
    };


    GcThread gcThread;

    ParallelTest test;
    test.add(doCreatorThread, CreatorThreads);
    test.run();

    gcThread.join();

    for (const auto& c: counters)
        locklessCheckEq(c.load(), size_t(CreatorThreads * Threads), NullLog);
}
