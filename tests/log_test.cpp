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
#include <thread>
#include <memory>

using namespace std;
using namespace lockless;

BOOST_AUTO_TEST_CASE(basic_test)
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

#if 0 // Trouble getting this to compile.
    setup();
    auto d2 = logMerge(Log<10>(), l0, l1, l2).dump();
    locklessCheckEq(d2.size(), 9);
    locklessCheck(is_sorted(d2.begin(), d2.end()));

    setup();
    auto d3 = logMerge(Log<10>(), l2, l0, l1).dump();
    locklessCheck(is_sorted(d3.begin(), d3.end()));
    locklessCheck(equal(d2.begin(), d2.end(), d3.begin()));
#endif
}

BOOST_AUTO_TEST_CASE(parallel_test)
{
    enum {
        LogThreads = 16,
        DumpThreads = 16,
        Iterations = 1000
    };

    Log<1024> logger;

    array<atomic<size_t>, LogThreads> counters;
    for (auto& c : counters) c.store(0);

    std::atomic<size_t> done(0);

    auto doLogThread = [&] (unsigned id) {
        for (size_t i = 0; i < Iterations; ++i)
            logger.log(static_cast<LogType>(id), "", "");

        done++;
    };

    auto doDumpThread = [&] () {
        bool exit = false;

        while (true) {
            auto dump = logger.dump();

            for (const auto& entry : dump)
                counters[static_cast<unsigned>(entry.type)]++;

            // Ensures that we do one last dump before we quit.
            if (exit) break;
            exit = done.load() == LogThreads;
        }
    };

    array<unique_ptr<thread>, LogThreads> logThreads;
    for (unsigned id = 0; id < LogThreads; ++id)
        logThreads[id].reset(new thread(bind(doLogThread, id)));

    array<unique_ptr<thread>, DumpThreads> dumpThreads;
    for (auto& th : dumpThreads)
        th.reset(new thread(doDumpThread));

    for (auto& th : logThreads) th->join();
    for (auto& th : dumpThreads) th->join();

    // There's no way we can guarantee to catch all the logs unless we have
    // enough cores. 2 cores just won't cut it here.
#if 0
    for (auto& c : counters)
        locklessCheckEq(c.load(), Iterations);
#endif
}
