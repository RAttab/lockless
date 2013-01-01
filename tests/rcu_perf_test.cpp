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


atomic<size_t> counter;

void doThread(Rcu& rcu, promise<double>& latency, unsigned iterations)
{
    Timer tm;

    for (size_t it = 0; it < iterations; ++it) {
        RcuGuard guard(rcu);
        rcu.defer([] { counter++; });
    }

    latency.set_value(tm.elapsed() / iterations);
}

pair<double, TimeDist>
doTest(unsigned threadCount, size_t iterations)
{
    vector<thread> threads;
    vector<promise<double> > latencies;
    TimeDist latencyDist;
    double throughput;

    counter.store(0);

    // run the test.
    {
        Rcu rcu;

        Timer tm;

        latencies.reserve(threadCount);
        threads.reserve(threadCount);

        for (size_t th = 0; th < threadCount; ++th) {
            latencies.emplace_back();
            threads.emplace_back(
                    doThread, ref(rcu), ref(latencies[th]), iterations);
        }

        for(auto& latency : latencies)
            latencyDist.add(latency.get_future().get());

        throughput = (threadCount * iterations) / tm.elapsed();
    }

    // cleanup the threads.
    for (auto& th : threads) th.join();

    return make_pair(throughput, latencyDist);
}

int main(int argc, char** argv)
{
    unsigned threadCount = 1;
    if (argc > 1) threadCount = stoul(string(argv[1]));

    size_t iterations = 100000;
    if (argc > 2) iterations = stoull(string(argv[2]));

    bool csvOutput = false;
    if (argc > 3) csvOutput = stoi(string(argv[3]));


    TimeDist throughputDist;
    TimeDist latencyDist;

    const double epsilon = 0.0000000001;
    double prev = -1;

    const unsigned min = 1, max = 1;
    unsigned attempts = 0;

    while (true) {
        auto ret = doTest(threadCount, iterations);

        throughputDist.add(ret.first);

        prev = latencyDist.stderr();
        latencyDist += ret.second;

        ++attempts;
        if (attempts < min) continue;
        if (attempts >= max) break;
        if (std::abs(prev - latencyDist.stderr()) < epsilon)
            break;
    }

    if (csvOutput) {
        printf( "%d,%ld,"
                "%.9f,%.9f,%.9f,%.9f,"
                "%ld,%ld,%ld,%ld\n",
                threadCount,
                iterations,

                latencyDist.min(), latencyDist.median(),
                latencyDist.max(), latencyDist.stderr(),

                static_cast<size_t>(throughputDist.min()),
                static_cast<size_t>(throughputDist.median()),
                static_cast<size_t>(throughputDist.max()),
                static_cast<size_t>(throughputDist.stderr()));
    }
    else {
        printf( "| th=%3d | it=%s "
                "| s/ops=[ %s, %s, %s ] e=%s "
                "| ops/s=[ %s, %s, %s ] e=%s\n",

                threadCount,
                fmtValue(iterations).c_str(),

                fmtElapsed(latencyDist.min()).c_str(),
                fmtElapsed(latencyDist.median()).c_str(),
                fmtElapsed(latencyDist.max()).c_str(),
                fmtElapsed(latencyDist.stderr()).c_str(),

                fmtValue(throughputDist.min()).c_str(),
                fmtValue(throughputDist.median()).c_str(),
                fmtValue(throughputDist.max()).c_str(),
                fmtValue(throughputDist.stderr()).c_str());
    }

    return 0;
}
