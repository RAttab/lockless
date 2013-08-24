/* perf_utils.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 17 Mar 2013
   FreeBSD-style copyright and disclaimer apply

   Utilities for performance tests.
*/

#ifndef __lockless__perf_utils_h__
#define __lockless__perf_utils_h__

#include "tm.h"
#include "test_utils.h"

#include <set>
#include <cmath>


namespace lockless {


/******************************************************************************/
/* TIME DIST                                                                  */
/******************************************************************************/

struct TimeDist
{
    void add(double ts) { dist.insert(ts); }

    double min() const { return *dist.begin(); }
    double max() const { return *dist.rbegin(); }

    double median() const
    {
        auto it = dist.begin();
        std::advance(it, dist.size() / 2);
        return *it;
    }

    double avg() const
    {
        double sum = std::accumulate(dist.begin(), dist.end(), 0.0);
        return sum / dist.size();
    }

    double variance() const
    {
        double u = avg();

        auto diffSqrt = [=] (double total, double x) {
            return total + std::pow(u - x, 2);
        };

        double sum = std::accumulate(dist.begin(), dist.end(), 0.0, diffSqrt);
        return sum / dist.size();
    }

    double stddev() const
    {
        return std::sqrt(variance());
    }

    TimeDist& operator+= (const TimeDist& other)
    {
        dist.insert(other.dist.begin(), other.dist.end());
        return *this;
    }

private:

    std::multiset<double> dist;

};


/******************************************************************************/
/* PERF TEST                                                                  */
/******************************************************************************/

template<typename Context>
struct PerfTest
{
    typedef std::function<void(Context&, unsigned)> TestFn;

    unsigned add(const TestFn& fn, unsigned thCount, size_t itCount)
    {
        configs.push_back(std::make_tuple(fn, thCount, itCount));
        return configs.size() - 1;
    }

    std::pair<TimeDist, TimeDist> distributions(unsigned gr) const
    {
        return std::make_pair(latencies[gr], throughputs[gr]);
    }

    unsigned threadCount(unsigned gr) const
    {
        return std::get<1>(configs[gr]);
    }

    size_t iterationCount(unsigned gr) const
    {
        return std::get<2>(configs[gr]);
    }

    void run()
    {
        latencies.resize(configs.size());
        throughputs.resize(configs.size());

        std::vector< std::vector< std::future<double> > > futures;
        futures.resize(configs.size());

        for (size_t th = 0; th < configs.size(); ++th)
            futures[th].reserve(std::get<1>(configs[th]));

        Context ctx;

        auto doTask = [&] (const TestFn& fn, unsigned itCount) -> double {
            Timer tm;
            fn(ctx, itCount);
            return tm.elapsed();
        };

        std::vector<unsigned> thCounts;
        for (size_t i = 0; i < configs.size(); ++i)
            thCounts.push_back(std::get<1>(configs[i]));

        Timer throughputTm;

        // Create the threads round robin-style.
        size_t remaining = 0;
        do {
            remaining = 0;
            for (size_t th = 0; th < configs.size(); ++th) {
                const TestFn& testFn = std::get<0>(configs[th]);
                unsigned& thCount    = thCounts[th];
                size_t itCount       = std::get<2>(configs[th]);

                if (!thCount) continue;
                remaining += --thCount;

                std::packaged_task<double()> task(
                        std::bind(doTask, testFn, itCount));

                futures[th].emplace_back(std::move(task.get_future()));
                std::thread(std::move(task)).detach();
            }
        } while (remaining > 0);

        // Poll each futures until they become available.
        do {
            remaining = 0;
            unsigned processed = 0;

            for (size_t i = 0; i < futures.size(); ++i) {
                for (size_t j = 0; j < futures[i].size(); ++j) {
                    if (!futures[i][j].valid()) continue;

                    auto ret = futures[i][j].wait_for(std::chrono::seconds(0));
                    if (ret == std::future_status::timeout) {
                        remaining++;
                        continue;
                    }

                    size_t itCount = std::get<2>(configs[i]);
                    double latency = futures[i][j].get();

                    latencies[i].add(latency / itCount);
                    throughputs[i].add(itCount / throughputTm.elapsed());

                    processed++;
                }
            }

            if (!processed)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));

        } while (remaining > 0);
    }

private:

    std::vector< std::tuple<TestFn, unsigned, size_t> > configs;

    std::vector<TimeDist> latencies;
    std::vector<TimeDist> throughputs;

};



/******************************************************************************/
/* DUMP                                                                       */
/******************************************************************************/

namespace details {

std::string
dumpHuman(
        const TimeDist& latency,
        const TimeDist& throughput,
        unsigned thCount, size_t itCount,
        const std::string& title)
{
    std::array<char, 256> buffer;
    snprintf(buffer.data(), buffer.size(),

            "| %12s th=%3d it=%s "
            "| s/ops=[ %s, %s, %s ] "
            "| ops/s=[ %s, %s, %s ]",

            title.c_str(), thCount, fmtValue(itCount).c_str(),

            fmtElapsed(latency.min()).c_str(),
            fmtElapsed(latency.median()).c_str(),
            fmtElapsed(latency.max()).c_str(),

            fmtValue(throughput.min()).c_str(),
            fmtValue(throughput.median()).c_str(),
            fmtValue(throughput.max()).c_str());

    return std::string(buffer.data());
}

std::string
dumpCsv(
        const TimeDist& latency,
        const TimeDist& throughput,
        unsigned thCount, size_t itCount,
        const std::string& title)
{
    std::array<char, 256> buffer;
    snprintf(buffer.data(), buffer.size(),

            "%s,%d,%ld,"
            "%.9f,%.9f,%.9f,%.9f,"
            "%ld,%ld,%ld,%ld",

            title.c_str(), thCount, itCount,

            latency.min(), latency.median(),
            latency.max(), latency.stddev(),

            static_cast<size_t>(throughput.min()),
            static_cast<size_t>(throughput.median()),
            static_cast<size_t>(throughput.max()),
            static_cast<size_t>(throughput.stddev()));

    return std::string(buffer.data());
}

} // namespace details


enum Format { Human, Csv };

template<typename Perf>
std::string
dump(   const Perf& perf,
        unsigned gr,
        const std::string& title,
        Format fmt = Human)
{
    unsigned thCount = perf.threadCount(gr);
    unsigned itCount = perf.iterationCount(gr);

    auto dist = perf.distributions(gr);
    const TimeDist& latency = dist.first;
    const TimeDist& throughput = dist.second;

    using namespace details;

    if (fmt == Human)
        return dumpHuman(latency, throughput, thCount, itCount, title);

    if (fmt == Csv)
        return dumpCsv(latency, throughput, thCount, itCount, title);

    return "<Unknown format>";
}


} // lockless

#endif // __lockless__perf_utils_h__
