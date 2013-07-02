/* tls_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 14 Apr 2013
   FreeBSD-style copyright and disclaimer apply

   Tests for the thread local storage class.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "tls.h"
#include "check.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>

using namespace std;
using namespace lockless;

struct Counters
{
    atomic<size_t> constructs;
    atomic<size_t> destructs;

    Counters() : constructs(0), destructs(0) {}
};

struct Barrier
{
    Barrier(size_t limit) : limit(limit), count(0) {}

    void wait()
    {
        size_t oldCount = count.fetch_add(1) + 1;
        while (oldCount != limit) oldCount = count;
    }

    const size_t limit;
    std::atomic<size_t> count;
};

BOOST_AUTO_TEST_CASE(test_single_tls)
{
    enum { Threads = 10 };

    struct Test0;
    Tls<size_t, Test0> tls;

    *tls = -1ULL;
    locklessCheckEq(*tls, -1ULL, NullLog);


    Barrier barrier(Threads);

    auto doTls = [&](unsigned id) {
        *tls = id;
        locklessCheckEq(*tls, id, NullLog);
        barrier.wait();
        locklessCheckEq(*tls, id, NullLog);
    };

    ParallelTest test;
    test.add(doTls, Threads);
    test.run();

    locklessCheckEq(*tls, -1ULL, NullLog);
}


BOOST_AUTO_TEST_CASE(test_single_tls_cons)
{
    enum { Threads = 10 };
    const size_t magic = 0xDEADBEEF;

    Counters count;

    auto construct = [&](size_t& tls) {
        count.constructs++;
        tls = magic;
    };

    auto destruct = [&](size_t& tls) {
        locklessCheckEq(tls, magic, NullLog);
        count.destructs++;
    };

    struct Test1;
    Tls<size_t, Test1> tls(construct, destruct);
    locklessCheckEq(count.constructs.load(), 0ULL, NullLog);

    *tls = -1ULL;
    locklessCheckEq(count.constructs.load(), 1ULL, NullLog);


    Barrier barrierStartCheck(Threads + 1);
    Barrier barrierDoneCheck(Threads + 1);

    auto doTls = [&](unsigned) {
        size_t value = *tls;
        locklessCheckEq(value, magic, NullLog);

        barrierStartCheck.wait();
        barrierDoneCheck.wait();

        *tls = value;
    };

    auto doCheck = [&](unsigned) {
        barrierStartCheck.wait();

        locklessCheckEq(count.constructs.load(), Threads + 1ULL, NullLog);
        locklessCheckEq(count.destructs.load(), 0ULL, NullLog);

        barrierDoneCheck.wait();
    };

    ParallelTest test;
    test.add(doTls, Threads);
    test.add(doCheck, 1);
    test.run();

    locklessCheckEq(count.constructs.load(), Threads + 1ULL, NullLog);
    locklessCheckEq(count.destructs.load(), size_t(Threads), NullLog);
}


