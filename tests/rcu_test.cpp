/* rcu_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 30 Dec 2012
   FreeBSD-style copyright and disclaimer apply

   Tests for the RCU library.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

// #define LOCKLESS_RCU_DEBUG 1

#include "rcu.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>
#include <iostream>
#include <random>
#include <map>
#include <memory>
#include <thread>

using namespace std;
using namespace lockless;

BOOST_AUTO_TEST_CASE(smoke_test)
{
    Rcu rcu;
    RcuGuard guard{rcu};
}

BOOST_AUTO_TEST_CASE(epoch_test)
{
    Rcu rcu;

    size_t e0 = rcu.enter();
    size_t e1 = rcu.enter();
    size_t e2 = rcu.enter();

    BOOST_CHECK_NE(e0, e1);
    BOOST_CHECK_EQUAL(e1, e2);
    rcu.exit(e2);

    size_t e3 = rcu.enter();
    BOOST_CHECK_EQUAL(e2, e3);

    rcu.exit(e0);
    size_t e4 = rcu.enter();
    BOOST_CHECK_NE(e3, e4);

    size_t e5 = rcu.enter();
    BOOST_CHECK_EQUAL(e4, e5);

    rcu.exit(e1);
    rcu.exit(e3);
    rcu.exit(e4);
    rcu.exit(e5);
}

BOOST_AUTO_TEST_CASE(simple_defer_test)
{
    Rcu rcu;

    int deferred = 0;
    auto deferFn = [&] { deferred++; };

    size_t e0 = rcu.enter();
    rcu.defer(deferFn);

    size_t e1 = rcu.enter();
    rcu.defer(deferFn);
    BOOST_CHECK_EQUAL(rcu.enter(), e1);
    rcu.exit(e0);

    BOOST_CHECK_EQUAL(deferred, 0);

    size_t e2 = rcu.enter();
    BOOST_CHECK_EQUAL(deferred, 1);

    rcu.exit(e1);
    rcu.exit(e1);
    size_t e3 = rcu.enter();
    BOOST_CHECK_EQUAL(deferred, 2);

    rcu.exit(e2);
    rcu.exit(e3);
}

BOOST_AUTO_TEST_CASE(complex_defer_test)
{
    Rcu rcu;

    array<unsigned, 10> counters;
    for (unsigned& c : counters) c = 0;

    for (size_t i = 0; i < counters.size(); ++i) {
        for (size_t j = 0; j < i; ++j)
            rcu.defer([&, i] { counters[i]++; });

        BOOST_CHECK_EQUAL(rcu.enter(), i + 1);
        if (i > 0) rcu.exit(i);

        for (size_t j = 0; j < counters.size(); ++j) {
            if (i > 0 && j < i) BOOST_CHECK_EQUAL(counters[j], j);
            else BOOST_CHECK_EQUAL(counters[j], 0);
        }
    }

    rcu.exit(counters.size());
}

BOOST_AUTO_TEST_CASE(destructor_defer_test)
{
    unsigned counter = 0;
    auto deferFn = [&] { counter++; };

    {
        Rcu rcu;

        rcu.defer(deferFn);
        rcu.enter();
        rcu.defer(deferFn);

        BOOST_CHECK_EQUAL(counter, 0);
    }

    BOOST_CHECK_EQUAL(counter, 2);
}

BOOST_AUTO_TEST_CASE(fuzz_test)
{
    GlobalLog.dump();

    map<size_t, size_t> expected;
    map<size_t, size_t> counters;

    {
        Rcu rcu;

        mt19937_64 engine;
        uniform_int_distribution<unsigned> rnd(0, 6);

        array<size_t, 2> epochs;
        array<size_t, 2> inEpochs;

        for (size_t i = 0; i < 2; ++i)
            epochs[i] = inEpochs[i] = 0;

        for (size_t i = 0; i < 1000; ++i) {
            unsigned action = rnd(engine);

            if (action == 0) {
                size_t e = rcu.enter();

                // If there's an empty slot, take it.
                if (!inEpochs[0]) epochs[0] = e;
                else if (!inEpochs[1]) epochs[1] = e;

                // Increment that slot's counter.
                if (epochs[0] == e) inEpochs[0]++;
                else if (epochs[1] == e) inEpochs[1]++;

                else assert(false);
            }

            else if (action == 1) {
                // Pick an epoch to exit at semi-random.
                size_t j = i % 2;
                if (!inEpochs[j]) j = (j + 1) % 2;
                if (!inEpochs[j]) continue;

                rcu.exit(epochs[j]);
                inEpochs[j]--;
            }

            else {
                // Add some deferred work.
                size_t j = std::max(epochs[0], epochs[1]);
                rcu.defer([&, j] { counters[j]++; });
                expected[j]++;
            }
        }
    }

    for (const auto& exp: expected)
        BOOST_CHECK_EQUAL(counters[exp.first], exp.second);
}

BOOST_AUTO_TEST_CASE(simple_parallel_test)
{
    enum {
        Threads = 256,
        Iterations = 1000,
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

        array<unique_ptr<thread>, Threads> threads;

        for (size_t id = 0; id < Threads; ++id)
            threads[id].reset(new thread(bind(doThread, id)));

        for (auto& th : threads)
            th->join();
    }

    for (size_t i = 0; i < counters.size(); ++i)
        BOOST_CHECK_EQUAL(counters[i], Iterations);
}

BOOST_AUTO_TEST_CASE(complex_parallel_test)
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
        void check() const { assert(value == MAGIC_VALUE); }
    };

    struct Context
    {
        Rcu rcu;
        array< atomic<Obj*>, Slots> slots;
        atomic<unsigned> doneCount;

        Context()
        {
            for (auto& obj : slots) obj.store(nullptr);
            doneCount.store(0);
        }

        ~Context()
        {
            for (auto& obj : slots) {
                if (!obj.load()) delete obj.load();
            }
        }

        void writeSlot(size_t index)
        {
            Obj* obj = slots[index].exchange(new Obj());
            if (obj) rcu.defer([=] { delete obj; });
        }

        void readSlot(size_t index)
        {
            Obj* obj = slots[index].load();
            if (!obj) return;
            obj->check();
        }
    };

    auto doWriteThread = [] (Context& ctx, unsigned itCount) {

        for (size_t it = 0; it < itCount; ++it) {
            RcuGuard guard(ctx.rcu);

            for (size_t index = 0; index < Slots; ++index)
                ctx.writeSlot(index);

            for (size_t index = Slots; index > 0; --index)
                ctx.writeSlot(index - 1);
        }

        ctx.doneCount++;
    };

    auto doReadThread = [] (Context& ctx, unsigned) {
        do {
            RcuGuard guard(ctx.rcu);

            for (size_t index = 0; index < Slots; ++index)
                ctx.readSlot(index);

        } while (ctx.doneCount.load() < WriteThreads);
    };

    PerfTest<Context> test;

    test.add(doWriteThread, WriteThreads, Iterations);
    test.add(doReadThread, ReadThreads, Iterations);

    test.run();
}
