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
    cerr << fmtTitle("simpleTest", '=') << endl;

    enum {
        Threads = 8,
        Iterations = 50000,
    };

    array<atomic<size_t>, Threads> counters;
    for (auto& c : counters) c.store(0);

    {
        Rcu rcu;

        auto doThread = [&] (unsigned id) {
            for (size_t i = 0; i < Iterations; ++i) {
                RcuGuard<Rcu> guard(rcu);
                rcu.defer([&, id] { counters[id]++; });
            }
        };

        ParallelTest test;
        test.add(doThread, Threads);
        test.run();
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

    Rcu rcu;
    Log<1024000> testLog;
    LogAggregator log(rcu.log, testLog);

    const uint64_t MAGIC_VALUE = 0xDEADBEEFDEADBEEFULL;
    struct Obj
    {
        uint64_t value;

        Obj() : value(MAGIC_VALUE) {}
        ~Obj() { value = 0; }
    };

    auto check = [&] (Obj* obj) {
        if (!obj) return;
        locklessCheckEq(obj->value, MAGIC_VALUE, log);
    };
    auto destroy = [&] (Obj* obj) {
        if (!obj) return;
        testLog(LogMisc, "destroy", "obj=%p", obj);
        check(obj);
        delete obj;
    };

    atomic<unsigned> doneCount(0);
    array< atomic<Obj*>, Slots> slots;
    for (auto& obj : slots) obj.store(nullptr);

    auto doWriteThread = [&] (unsigned) {
        for (size_t it = 0; it < Iterations; ++it) {
            RcuGuard<Rcu> guard(rcu);

            for (size_t index = Slots; index > 0; --index) {
                Obj* newObj = new Obj();
                Obj* oldObj = slots[index - 1].exchange(newObj);
                testLog(LogMisc, "write", "index=%ld, new=%p, old=%p",
                        index - 1, newObj, oldObj);

                if (oldObj) rcu.defer( [=] { destroy(oldObj); });
            }
        }

        doneCount++;
    };

    auto doReadThread = [&] (unsigned) {
        do {
            RcuGuard<Rcu> guard(rcu);

            for (size_t index = 0; index < Slots; ++index) {
                Obj* obj = slots[index];
                testLog(LogMisc, "read", "index=%ld, obj=%p", index, obj);
                check(obj);
            }

        } while (doneCount.load() < WriteThreads);
    };

    ParallelTest test;
    test.add(doWriteThread, WriteThreads);
    test.add(doReadThread, ReadThreads);
    test.run();

    testLog(LogMisc, "cleanup", "");
    for (auto& obj : slots) destroy(obj.load());
}
