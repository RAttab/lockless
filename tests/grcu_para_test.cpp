/* grcu_para_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 16 May 2013
   FreeBSD-style copyright and disclaimer apply

   Parallel tests for the global rcu implementation.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#define LOCKLESS_GRCU_DEBUG 1
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
        Iterations = 100000,
    };

    array<atomic<size_t>, Threads> counters;
    for (auto& c : counters) c.store(0);

    array<atomic<size_t>, 2> inEpoch;
    for (auto& e : inEpoch) e.store(0);

    {
        auto doThread = [&] (unsigned id) {
            GlobalRcu rcu;
            for (size_t i = 0; i < Iterations; ++i) {
                size_t epoch = rcu.enter();
                inEpoch[epoch & 1]++;

                rcu.defer([&, id, epoch] {
                            locklessCheckEq(inEpoch[epoch & 1], 0ULL, rcu.log());
                            counters[id]++;
                        });

                inEpoch[epoch & 1]--;
                rcu.exit(epoch);
            }
        };

        ParallelTest test;
        test.add(doThread, Threads);
        test.run();
    }

    for (size_t i = 0; i < counters.size(); ++i)
        locklessCheckEq(counters[i], Iterations, NullLog);
}
