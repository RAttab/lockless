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
    }

    logToStream(GcThread().log());

    for (size_t i = 0; i < counters.size(); ++i)
        locklessCheckEq(counters[i], Iterations, NullLog);
}
