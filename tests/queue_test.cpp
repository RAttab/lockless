/* queue_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 16 Mar 2013
   FreeBSD-style copyright and disclaimer apply

   Tests for the unbounded lock-free queue.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#define LOCKLESS_QUEUE_DEBUG 1

#include "queue.h"
#include "check.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>
#include <queue>
#include <random>

using namespace std;
using namespace lockless;

BOOST_AUTO_TEST_CASE(basicTest)
{
    cerr << fmtTitle("basic", '=') << endl;

    Queue<size_t> queue;
    auto log = queue.allLogs();

    for (size_t k = 0; k < 100; ++k) {
        checkPair(queue.peek(), log, locklessCtx());

        for (size_t i = 0; i < 100; ++i) {
            queue.push(i);
            checkPair(queue.peek(), size_t(0), log, locklessCtx());
        }

        for (size_t i = 0; i < 100; ++i) {
            checkPair(queue.peek(), i, log, locklessCtx());
            checkPair(queue.pop(), i, log, locklessCtx());
        }

        checkPair(queue.pop(), log, locklessCtx());
        checkPair(queue.peek(), log, locklessCtx());
    }
}

BOOST_AUTO_TEST_CASE(interleavedTest)
{
    cerr << fmtTitle("interleaved", '=') << endl;

    mt19937_64 rng;

    Queue<string> queue;
    auto log = queue.allLogs();

    for (size_t i = 0; i < 100; ++i) {
        string value = randomString(10, rng);

        checkPair(queue.peek(), log, locklessCtx());
        queue.push(value);
        checkPair(queue.peek(), value, log, locklessCtx());
        checkPair(queue.pop(), value, log, locklessCtx());
        checkPair(queue.peek(), log, locklessCtx());
    }
}

BOOST_AUTO_TEST_CASE(fuzzTest)
{
    cerr << fmtTitle("fuzz", '=') << endl;

    mt19937_64 rng;
    binomial_distribution<bool> opDist;
    uniform_int_distribution<size_t> valueDist(0, -1);

    Queue<size_t> queue;
    std::queue<size_t> refQueue; // std reference queue.
    auto log = queue.allLogs();

    auto push = [&] {
        size_t value = valueDist(rng);
        queue.push(value);
        refQueue.push(value);
    };

    auto pop = [&] {
        size_t value = refQueue.front();
        refQueue.pop();
        checkPair(queue.pop(), value, log, locklessCtx());
    };

    for (size_t it = 0; it < 10000; ++it) {

        if (refQueue.empty() || opDist(rng))
            push();
        else pop();
    }

    // Empty out the queue.
    while (!refQueue.empty()) pop();
    checkPair(queue.pop(), log, locklessCtx());
}
