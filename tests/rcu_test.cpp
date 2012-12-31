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

BOOST_AUTO_TEST_CASE( test_basics )
{
    Rcu rcu;

    {
        RcuGuard guard(rcu);
    }

    cerr << "=== FIRST ===" << endl;
    logToStream(GlobalLog);

    {
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

    cerr << "=== SECOND ===" << endl;
    logToStream(GlobalLog);

    int deferred = 0;
    auto deferFn = [&] { deferred++; };

    rcu.defer(deferFn);
    BOOST_CHECK(!deferred);

    cerr << "=== DEFER ===" << endl;
    logToStream(GlobalLog);

    {
        size_t e0 = rcu.enter();
        BOOST_CHECK_EQUAL(deferred, 0);

        size_t e1 = rcu.enter();
        BOOST_CHECK_EQUAL(deferred, 1);

        rcu.exit(e1);
        rcu.exit(e0);
        BOOST_CHECK_EQUAL(deferred, 1);
    }

    cerr << "=== DO_DEFER ===" << endl;
    logToStream(GlobalLog);

    {
        size_t e0 = rcu.enter();

        for (int i = 0; i < 3; ++i)
            rcu.defer(deferFn);

        size_t e1 = rcu.enter();
        BOOST_CHECK_EQUAL(deferred, 1);
        rcu.exit(e1);

        BOOST_CHECK_EQUAL(deferred, 1);
        rcu.exit(e0);

        size_t e3 = rcu.enter();
        BOOST_CHECK_EQUAL(deferred, 4);

        rcu.exit(e3);
    }

    cerr << "=== DONE ===" << endl;
    logToStream(GlobalLog);
}
