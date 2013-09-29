/* ring_para_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 28 Sep 2013
   FreeBSD-style copyright and disclaimer apply

   Parallel tests for the ring thingy.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#define LOCKLESS_CHECK_ABORT 1

#include <iostream>

#include "ring.h"
#include "check.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>
#include <bitset>

using namespace std;
using namespace lockless;

template<typename Ring>
void testQueue(const string& title, unsigned popTh, unsigned pushTh)
{
    string fmt = format(
            "queue - %s-%lld - %d-%d",
            title.c_str(), Ring::Size, popTh, pushTh);
    cerr << fmtTitle(fmt, '=') << endl;

    enum { Iterations = 10000 };

    Ring ring;
    auto& log = NullLog;

    atomic<size_t> done(0);
    vector<size_t> sums(popTh, 0);

    auto doPush = [&] (unsigned id) {
        for (unsigned it = 1; it <= Iterations; ++it)
            while (!ring.push(it + id * Iterations));
        done++;
    };

    auto doPop = [&] (unsigned id) {
        while (done < pushTh || ring.size())
            sums[id] += ring.pop();
   };

    ParallelTest test;
    test.add(doPush, pushTh);
    test.add(doPop, popTh);
    test.run();

    size_t n = pushTh * Iterations;
    size_t sum = accumulate(sums.begin(), sums.end(), size_t(0));
    locklessCheckEq(sum, (n*(n+1)) / 2, log);
}

BOOST_AUTO_TEST_CASE(queue)
{
    enum { n = 8 };

    testQueue< RingQueueSRSW<size_t, 1> >("srsw", 1, 1);
    testQueue< RingQueueSRSW<size_t, 8> >("srsw", 1, 1);

    testQueue< RingQueueMRMW<size_t, 1> >("mrmw", 1, 1);
    testQueue< RingQueueMRMW<size_t, 8> >("mrmw", 1, 1);

    testQueue< RingQueueMRMW<size_t, 1> >("mrmw", 1, n);
    testQueue< RingQueueMRMW<size_t, 8> >("mrmw", 1, n);

    testQueue< RingQueueMRMW<size_t, 1> >("mrmw", n, 1);
    testQueue< RingQueueMRMW<size_t, 8> >("mrmw", n, 1);

    testQueue< RingQueueMRMW<size_t, 1> >("mrmw", n, n);
    testQueue< RingQueueMRMW<size_t, 8> >("mrmw", n, n);
}
