/* rcu_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 30 Dec 2012
   FreeBSD-style copyright and disclaimer apply

   Tests for the RCU library.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#define LOCKLESS_RCU_DEBUG 1

#include "rcu.h"
#include "check.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>
#include <iostream>
#include <random>
#include <map>
#include <memory>

using namespace std;
using namespace lockless;


BOOST_AUTO_TEST_CASE(smokeTest)
{
    Rcu rcu;
    RcuGuard<Rcu> guard{rcu};
}


BOOST_AUTO_TEST_CASE(epochTest)
{
    cerr << fmtTitle("epochTest", '=') << endl;

    Rcu rcu;
    auto& log = rcu.log;

    for (size_t i = 0; i < 5; ++i) {
        // cerr << fmtTitle("a, 0, 0") << endl;
        size_t e0 = rcu.enter();
        size_t e1 = rcu.enter();
        locklessCheckNe(e1, e0, log);

        // cerr << fmtTitle("b, 1, 1") << endl;
        locklessCheckEq(rcu.enter(), e1, log);
        rcu.exit(e1);

        // cerr << fmtTitle("c, 1, 1") << endl;
        locklessCheckEq(rcu.enter(), e1, log);
        rcu.exit(e1);

        // cerr << fmtTitle("d, 1, 1") << endl;
        rcu.exit(e0);

        // cerr << fmtTitle("e, 1, 0") << endl;
        locklessCheckEq(rcu.enter(), e1, log);
        rcu.exit(e1);

        // cerr << fmtTitle("f, 0, 1") << endl;
        size_t e2 = rcu.enter();
        locklessCheckNe(e2, e1, log);
        rcu.exit(e2);

        // cerr << fmtTitle("g, 0, 1") << endl;
        rcu.exit(e1);

        // cerr << fmtTitle("h, 0, 0") << endl;
    }
}


BOOST_AUTO_TEST_CASE(simpleDeferTest)
{
    cerr << fmtTitle("simpleDeferTest", '=') << endl;

    Rcu rcu;
    auto& log = rcu.log;

    for (size_t i = 0; i < 5; ++i) {
        int deferred = 0;
        auto deferFn = [&] { deferred++; };

        // cerr << fmtTitle("a, 0-0, 0-0") << endl;
        rcu.defer(deferFn);
        size_t e0 = rcu.enter();

        // cerr << fmtTitle("b, 0-0, 1-1") << endl;
        rcu.defer(deferFn);
        size_t e1 = rcu.enter();

        // cerr << fmtTitle("c, 1-1, 1-1") << endl;
        locklessCheckEq(rcu.enter(), e1, log);
        rcu.exit(e0);
        locklessCheckEq(deferred, 1, log);

        // cerr << fmtTitle("d, 2-1, 0-0") << endl;
        locklessCheckEq(rcu.enter(), e1, log);
        locklessCheckEq(deferred, 1, log);

        // cerr << fmtTitle("e, 0-0, 2-1") << endl;
        rcu.exit(e1);
        rcu.exit(e1);
        rcu.exit(e1);
        locklessCheckEq(deferred, 2, log);

        // cerr << fmtTitle("f, 0-0, 0-0") << endl;
    }
}


BOOST_AUTO_TEST_CASE(complexDeferTest)
{
    cerr << fmtTitle("complexDeferTest", '=') << endl;

    Rcu rcu;
    auto& log = rcu.log;

    array<unsigned, 10> counters;
    for (unsigned& c : counters) c = 0;

    for (size_t i = 0; i < counters.size(); ++i) {
        for (size_t j = 0; j < i; ++j)
            rcu.defer([&, i] { counters[i]++; });

        if (i > 0) rcu.exit(i - 1);
        locklessCheckEq(rcu.enter(), i, log);

        for (size_t j = 0; j < counters.size(); ++j) {
            if (i > 0 && j < i) locklessCheckEq(counters[j], j, log);
            else locklessCheckEq(counters[j], 0U, log);
        }
    }

    rcu.exit(counters.size() - 1);
}


BOOST_AUTO_TEST_CASE(destructorDeferTest)
{
    cerr << fmtTitle("destructorDeferTest", '=') << endl;

    unsigned counter = 0;
    auto deferFn = [&] { counter++; };

    {
        Rcu rcu;

        rcu.defer(deferFn);
        rcu.enter();
        rcu.defer(deferFn);

        locklessCheckEq(counter, 0U, rcu.log);
    }

    locklessCheckEq(counter, 2U, NullLog);
}


BOOST_AUTO_TEST_CASE(fuzzTest)
{
    cerr << fmtTitle("fuzzTest", '=') << endl;

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

        for (size_t i = 0; i < 10000; ++i) {
            unsigned action = rnd(engine);

            if (action == 0) {
                size_t e = rcu.enter();

                if (epochs[0] == e) inEpochs[0]++;
                else if (epochs[1] == e) inEpochs[1]++;

                else if (!inEpochs[0]) { epochs[0] = e; inEpochs[0]++; }
                else if (!inEpochs[1]) { epochs[1] = e; inEpochs[1]++; }
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
        locklessCheckEq(counters[exp.first], exp.second, NullLog);
}
