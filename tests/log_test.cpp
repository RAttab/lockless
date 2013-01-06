/* log_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 31 Dec 2012
   FreeBSD-style copyright and disclaimer apply

   Tests for the log system.

*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "log.h"
#include "check.h"

#include <boost/test/unit_test.hpp>

using namespace std;
using namespace lockless;

BOOST_AUTO_TEST_CASE(basicTest)
{
    Log<2> logger;

    logger.log(LogRcu, "T0", "boo!");
    logger.log(LogMap, "T1", "number=%d", 42);

    auto d0 = logger.dump();
    locklessCheckEq(d0.size(), 2);
    locklessCheckEq(d0[0].type, LogRcu);
    locklessCheckEq(d0[0].title, "T0");
    locklessCheckEq(d0[1].type, LogMap);
    locklessCheckEq(d0[1].title, "T1");
    locklessCheckLt(d0[0].tick, d0[1].tick);

    logger.log(LogQueue, "T2", "blah");

    auto d1 = logger.dump();
    locklessCheckEq(d1.size(), 1);
    locklessCheckEq(d1[0].type, LogQueue);
    locklessCheckEq(d1[0].title, "T2");
}

BOOST_AUTO_TEST_CASE(mergeTest)
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
    locklessCheckEq(d0.size(), 2);
    locklessCheckEq(d0[0].title, "T5");
    locklessCheckEq(d0[1].title, "T7");
    locklessCheckLt(d0[0].tick, d0[1].tick);

    setup();
    Log<10> m1(l2, l1);
    auto d1 = m1.dump();
    locklessCheckEq(d1.size(), 7);
    locklessCheckEq(d1[0].title, "T2");
    locklessCheckEq(d1[1].title, "T3");
    locklessCheckEq(d1[2].title, "T4");
    locklessCheckEq(d1[3].title, "T5");
    locklessCheckEq(d1[4].title, "T6");
    locklessCheckEq(d1[5].title, "T7");
    locklessCheckEq(d1[6].title, "T8");
    locklessCheck(is_sorted(d1.begin(), d1.end()));

    setup();
    auto d2 = LogAggregator(l0, l1, l2).dump();
    locklessCheckEq(d2.size(), 9);
    locklessCheck(is_sorted(d2.begin(), d2.end()));

    auto eqFn = [] (const LogEntry& lhs, const LogEntry& rhs) {
        return lhs.type == rhs.type && lhs.title == rhs.title;
    };

    setup();
    auto d3 = LogAggregator(l2, l0, l1).dump();
    locklessCheck(equal(d2.begin(), d2.end(), d3.begin(), eqFn));

    setup();
    LogAggregator a1(l1, l2, l0);
    logToStream(a1);
    setup();
    auto d4 = a1.dump();
    locklessCheck(equal(d2.begin(), d2.end(), d4.begin(), eqFn));
}
