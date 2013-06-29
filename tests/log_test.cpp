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
    Log<2> log;

    log(LogRcu, "T0", "boo!");
    log(LogMap, "T1", "number=%d", 42);

    auto d0 = log.dump();
    locklessCheckEq(d0.size(), 2ULL, NullLog);
    locklessCheckEq(d0[0].type, LogRcu, NullLog);
    locklessCheckEq(d0[0].title, "T0", NullLog);
    locklessCheckEq(d0[1].type, LogMap, NullLog);
    locklessCheckEq(d0[1].title, "T1", NullLog);
    locklessCheckLt(d0[0].tick, d0[1].tick, NullLog);

    log(LogQueue, "T2", "blah");

    auto d1 = log.dump();
    locklessCheckEq(d1.size(), 1ULL, NullLog);
    locklessCheckEq(d1[0].type, LogQueue, NullLog);
    locklessCheckEq(d1[0].title, "T2", NullLog);
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
    auto d0 = LogAggregator(l0, l1, l2).dump();
    locklessCheckEq(d0.size(), 9ULL, NullLog);
    locklessCheck(is_sorted(d0.begin(), d0.end()), NullLog);

    auto eqFn = [] (const LogEntry& lhs, const LogEntry& rhs) {
        return lhs.type == rhs.type && lhs.title == rhs.title;
    };

    setup();
    auto d1 = LogAggregator(l2, l0, l1).dump();
    locklessCheck(equal(d0.begin(), d0.end(), d1.begin(), eqFn), NullLog);

    setup();
    LogAggregator a1(l1, l2, l0);
    logToStream(a1);
    setup();
    auto d2 = a1.dump();
    locklessCheck(equal(d0.begin(), d0.end(), d2.begin(), eqFn), NullLog);
}
