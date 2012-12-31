/* log_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 31 Dec 2012
   FreeBSD-style copyright and disclaimer apply

   Tests for the log system.

*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "log.h"

#include <boost/test/unit_test.hpp>

using namespace std;
using namespace lockless;

BOOST_AUTO_TEST_CASE(basic_test)
{
    Log<2> logger;

    logger.log(LogRcu, "T0", "boo!");
    logger.log(LogMap, "T1", "number=%d", 42);

    auto d0 = logger.dump();
    BOOST_CHECK_EQUAL(d0.size(), 2);
    BOOST_CHECK_EQUAL(d0[0].type, LogRcu);
    BOOST_CHECK_EQUAL(d0[0].title, "T0");
    BOOST_CHECK_EQUAL(d0[1].type, LogMap);
    BOOST_CHECK_EQUAL(d0[1].title, "T1");
    BOOST_CHECK_LT(d0[0].tick, d0[1].tick);

    logger.log(LogQueue, "T2", "blah");

    auto d1 = logger.dump();
    BOOST_CHECK_EQUAL(d1.size(), 1);
    BOOST_CHECK_EQUAL(d1[0].type, LogQueue);
    BOOST_CHECK_EQUAL(d1[0].title, "T2");
}

BOOST_AUTO_TEST_CASE(merge_test)
{

    Log<2> l0;
    Log<3> l1;
    Log<4> l2;

    auto setup = [&] {
        l0.dump();
        l1.dump();
        l2.dump();

        l0.log(LogRcu,   "T0", "");
        l0.log(LogRcu,   "T1", "");
        l2.log(LogQueue, "T2", "");
        l1.log(LogMap,   "T3", "");
        l2.log(LogQueue, "T4", "");
        l1.log(LogMap,   "T5", "");
        l2.log(LogQueue, "T6", "");
        l1.log(LogMap,   "T7", "");
        l2.log(LogQueue, "T8", "");
    };

    setup();
    Log<2> m0(l0, l1);
    auto d0 = m0.dump();
    BOOST_CHECK_EQUAL(d0.size(), 2);
    BOOST_CHECK_EQUAL(d0[0].title, "T5");
    BOOST_CHECK_EQUAL(d0[1].title, "T7");
    BOOST_CHECK_LT(d0[0].tick, d0[1].tick);

    setup();
    Log<10> m1(l2, l1);
    auto d1 = m1.dump();
    BOOST_CHECK_EQUAL(d1.size(), 7);
    BOOST_CHECK_EQUAL(d1[0].title, "T2");
    BOOST_CHECK_EQUAL(d1[1].title, "T3");
    BOOST_CHECK_EQUAL(d1[2].title, "T4");
    BOOST_CHECK_EQUAL(d1[3].title, "T5");
    BOOST_CHECK_EQUAL(d1[4].title, "T6");
    BOOST_CHECK_EQUAL(d1[5].title, "T7");
    BOOST_CHECK_EQUAL(d1[6].title, "T8");
    BOOST_CHECK(is_sorted(d1.begin(), d1.end()));

#if 0 // Trouble getting this to compile.
    setup();
    auto d2 = logMerge(Log<10>(), l0, l1, l2).dump();
    BOOST_CHECK_EQUAL(d2.size(), 9);
    BOOST_CHECK(is_sorted(d2.begin(), d2.end()));

    setup();
    auto d3 = logMerge(Log<10>(), l2, l0, l1).dump();
    BOOST_CHECK(is_sorted(d3.begin(), d3.end()));
    BOOST_CHECK(equal(d2.begin(), d2.end(), d3.begin()));
#endif
}
