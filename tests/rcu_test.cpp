/* rcu_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 30 Dec 2012
   FreeBSD-style copyright and disclaimer apply

   Tests for the RCU library.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "rcu.h"

#include <boost/test/unit_test.hpp>

using namespace std;
using namespace lockless;

BOOST_AUTO_TEST_CASE( test_basics )
{
    Rcu rcu;

    { 
        RcuGuard guard(rcu); 
    }

    { 
        RcuGuard guard0(rcu); 
        RcuGuard guard1(rcu); 
    }

    int defered = 0;
    auto deferFn = [&] { defered++; };
    
    rcu.defer(deferFn);
    BOOST_CHECK(!defered);

    {
        Rcu::Epoch* epoch = rcu.enter();
        BOOST_CHECK_EQUAL(defered, 1);

        rcu.exit(epoch);
        BOOST_CHECK_EQUAL(defered, 1);
    }

    {
        RcuGuard guard(rcu);

        for (int i = 0; i < 3; ++i)
            rcu.defer(deferFn);

        BOOST_CHECK_EQUAL(defered, 1);
    }

    BOOST_CHECK_EQUAL(defered, 4);
}
