/* rcu_perf_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 31 Dec 2012
   FreeBSD-style copyright and disclaimer apply

   Performance tests for RCU.
*/

#include "rcu.h"
#include "test_utils.h"

#include <atomic>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <future>
#include <cstdio>

using namespace std;
using namespace lockless;


/******************************************************************************/
/* OPS                                                                        */
/******************************************************************************/

struct Context
{
    Rcu rcu;
    atomic<size_t> counter;
};

void doEnterExitThread(Context& ctx, unsigned itCount)
{
    for (size_t it = 0; it < itCount; ++it)
        RcuGuard guard(ctx.rcu);
}

void doDeferThread(Context& ctx, unsigned itCount)
{
    for (size_t it = 0; it < itCount; ++it)
        ctx.rcu.defer([&] { ctx.counter++; });
}


/******************************************************************************/
/* DUMPS                                                                      */
/******************************************************************************/

void dumpCsvLine(
        const string& name,
        unsigned thCount, size_t itCount,
        const std::pair<TimeDist, TimeDist>& dists)
{
    const TimeDist& latency = dists.first;
    const TimeDist& throughput = dists.second;

    printf( "%s,%d,%ld,"
            "%.9f,%.9f,%.9f,%.9f,"
            "%ld,%ld,%ld,%ld\n",

            name.c_str(), thCount, itCount,

            latency.min(), latency.median(), latency.max(), latency.stddev(),

            static_cast<size_t>(throughput.min()),
            static_cast<size_t>(throughput.median()),
            static_cast<size_t>(throughput.max()),
            static_cast<size_t>(throughput.stddev()));
}

void dumpReadableLine(
        const string& name,
        unsigned thCount, size_t itCount,
        const std::pair<TimeDist, TimeDist>& dists)
{
    const TimeDist& latency = dists.first;
    const TimeDist& throughput = dists.second;

    printf( "| %8s th=%3d it=%s "
            "| s/ops=[ %s, %s, %s ] stddev=%s "
            "| ops/s=[ %s, %s, %s ] stddev=%s\n",

            name.c_str(), thCount, fmtValue(itCount).c_str(),

            fmtElapsed(latency.min()).c_str(),
            fmtElapsed(latency.median()).c_str(),
            fmtElapsed(latency.max()).c_str(),
            fmtElapsed(latency.stddev()).c_str(),

            fmtValue(throughput.min()).c_str(),
            fmtValue(throughput.median()).c_str(),
            fmtValue(throughput.max()).c_str(),
            fmtValue(throughput.stddev()).c_str());
}


/******************************************************************************/
/* MAIN                                                                       */
/******************************************************************************/

int main(int argc, char** argv)
{
    unsigned thCount = 1;
    if (argc > 1) thCount = stoul(string(argv[1]));

    size_t itCount = 100000;
    if (argc > 2) itCount = stoull(string(argv[2]));

    bool csvOutput = false;
    if (argc > 3) csvOutput = stoi(string(argv[3]));

    PerfTest<Context> perf;
    perf.add(doEnterExitThread, thCount, itCount);
    perf.add(doDeferThread, thCount, itCount);

    perf.run();

    array<string, 2> titles {{ "epochs", "defer" }};
    for (unsigned gr = 0; gr < 2; ++gr) {
        auto dists = perf.distributions(gr);

        if (csvOutput) dumpCsvLine(titles[gr], thCount, itCount, dists);
        else dumpReadableLine(titles[gr], thCount, itCount, dists);
    }

    return 0;
}
