/* log_para_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 05 Jan 2013
   FreeBSD-style copyright and disclaimer apply

   Parallel tests for Log
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#define LOCKLESS_LOG_DEBUG 1
#define LOCKLESS_CHECK_ABORT 1

#include "log.h"
#include "check.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>

using namespace std;
using namespace lockless;


BOOST_AUTO_TEST_CASE(theTest)
{
    enum {
        LogThreads = 16,
        DumpThreads = 16,
        Iterations = 1000
    };

    Log<1024> logger;

    array<atomic<size_t>, LogThreads> counters;
    for (auto& c : counters) c.store(0);

    atomic<size_t> done(0);

    auto doLogThread = [&] (unsigned id) {
        for (size_t i = 0; i < Iterations; ++i)
            logger.log(static_cast<LogType>(id), "", "");

        done++;
    };

    auto doDumpThread = [&] (unsigned) {
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

    ParallelTest test;
    test.add(doLogThread, LogThreads);
    test.add(doDumpThread, DumpThreads);
    test.run();

    // There's no way we can guarantee to catch all the logs unless we have
    // enough cores. 2 cores just won't cut it here.
#if 0
    for (auto& c : counters)
        locklessCheckEq(c.load(), Iterations);
#endif
}
