/* rcu_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 30 Dec 2012
   FreeBSD-style copyright and disclaimer apply

   Tests for the RCU library.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "rcu.h"

#include <boost/test/unit_test.hpp>
#include <iostream>

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


BOOST_AUTO_TEST_CASE( test_parallel )
{

}
