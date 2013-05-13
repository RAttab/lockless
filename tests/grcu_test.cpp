/* grcu_test.cc                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 28 Apr 2013
   FreeBSD-style copyright and disclaimer apply

   Tests for the global rcu implementation.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#define LOCKLESS_GRCU_DEBUG 1

#include "grcu.h"
#include "check.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>
#include <iostream>

using namespace std;
using namespace lockless;


BOOST_AUTO_TEST_CASE(smokeTest)
{
    cerr << fmtTitle("smokeTest", '=') << endl;

    GlobalRcu rcu;
    RcuGuard<GlobalRcu> guard(rcu);
    cerr << rcu.print() << endl;
}

BOOST_AUTO_TEST_CASE(epochTest)
{
    cerr << fmtTitle("epochTest", '=') << endl;

    GlobalRcu rcu;
    auto log = rcu.log();

    for (size_t i = 0; i < 5; ++i) {
        cerr << fmtTitle("a, 0, 0") << endl;
        cerr << rcu.print() << endl;


        size_t e0 = rcu.enter();
        locklessCheckEq(rcu.enter(), e0, log);
        cerr << fmtTitle("b, 2, 0") << endl;
        cerr << rcu.print() << endl;

        rcu.exit(e0);
        rcu.gc();
        size_t e1 = rcu.enter();
        cerr << fmtTitle("c, 1, 1") << endl;
        cerr << rcu.print() << endl;
        locklessCheckNe(e1, e0, log);

        rcu.gc();
        locklessCheckEq(rcu.enter(), e1, log);
        cerr << fmtTitle("d, 1, 2") << endl;
        cerr << rcu.print() << endl;

        rcu.exit(e0);
        rcu.gc();
        size_t e2 = rcu.enter();
        cerr << fmtTitle("e, 1, 2") << endl;
        cerr << rcu.print() << endl;
        locklessCheckNe(e2, e0, log);
        locklessCheckNe(e2, e1, log);

        rcu.exit(e2);
        rcu.exit(e1);
        rcu.exit(e1);
        rcu.gc();
        cerr << fmtTitle("f, 0, 0") << endl;
        cerr << rcu.print() << endl;
    }
}

void enterExit(GlobalRcu& rcu)
{
    thread([&] { RcuGuard<GlobalRcu> guard(rcu); }).join();
}
