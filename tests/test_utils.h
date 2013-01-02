/** test.h                                 -*- C++ -*-
    Rémi Attab (remi.attab@gmail.com), 24 Dec 2012
    FreeBSD-style copyright and disclaimer apply

    Testing framework for multithreaded algorithms

*/

#ifndef __lockess__test_h__
#define __lockess__test_h__

#include <time.h>
#include <unistd.h>
#include <set>
#include <functional>
#include <future>
#include <thread>
#include <cstdlib>
#include <cmath>

namespace lockless {

/******************************************************************************/
/* TIME                                                                       */
/******************************************************************************/

struct Time
{
    static double wall()
    {
        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
            return -1;

        return ts.tv_sec + (ts.tv_nsec * 0.0000000001);
    }
};


/******************************************************************************/
/* TIMER                                                                      */
/******************************************************************************/

struct Timer
{
    Timer() : start(Time::wall()) {}

    double elapsed() const
    {
        double end = Time::wall();
        double adj = Time::wall();
        return (end - start) - (adj - end);
    }

private:
    double start;
};


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
/* FORMAT                                                                     */
/******************************************************************************/

std::string fmtElapsed(double elapsed)
{
    char scale;

    if (elapsed >= 1.0) scale = 's';

    if (elapsed < 1.0) {
        elapsed *= 1000.0;
        scale = 'm';
    }

    if (elapsed < 1.0) {
        elapsed *= 1000.0;
        scale = 'u';
    }

    if (elapsed < 1.0) {
        elapsed *= 1000.0;
        scale = 'n';
    }

    std::array<char, 32> buffer;
    snprintf(buffer.data(), buffer.size(), "%6.2f%c", elapsed, scale);
    return std::string(buffer.data());
}


std::string fmtValue(double value)
{
    char scale;

    if (value >= 1.0) scale = ' ';

    if (value >= 1000.0) {
        value /= 1000.0;
        scale = 'k';
    }

    if (value >= 1000.0) {
        value /= 1000.0;
        scale = 'm';
    }

    if (value >= 1000.0) {
        value /= 1000.0;
        scale = 'g';
    }

    std::array<char, 32> buffer;
    snprintf(buffer.data(), buffer.size(), "%6.2f%c", value, scale);
    return std::string(buffer.data());
}


/******************************************************************************/
/* PARALLEL TEST                                                              */
/******************************************************************************/

struct ParallelTest
{
    typedef std::function<unsigned(unsigned)> TestFn;

    void add(const TestFn& fn, unsigned thCount)
    {
        configs.push_back(std::make_pair(fn, thCount));
    }

    unsigned run()
    {
        std::vector< std::vector< std::future<unsigned> > > futures;
        futures.resize(configs.size());
        for (size_t th = 0; th < configs.size(); ++th)
            futures[th].reserve(configs[th].second);

        // Create the threads round robin-style.
        size_t remaining;
        do {
            remaining = 0;
            for (size_t th = 0; th < configs.size(); ++th) {
                const TestFn& testFn = configs[th].first;
                unsigned& thCount    = configs[th].second;

                if (!thCount) continue;
                remaining += --thCount;

                std::packaged_task<unsigned()> task(std::bind(testFn, thCount));
                futures[th].emplace_back(std::move(task.get_future()));
                std::thread(std::move(task)).detach();
            }
        } while (remaining > 0);

        unsigned errors = 0;

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

                    errors += futures[i][j].get();
                    processed++;
                }
            }

            if (!processed)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));

        } while (remaining > 0);

        return errors;
    }

    std::vector< std::pair<TestFn, unsigned> > configs;
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

    std::pair<TimeDist, TimeDist> distributions(unsigned gr)
    {
        return std::make_pair(latencies[gr], throughputs[gr]);
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


        Timer throughputTm;

        // Create the threads round robin-style.
        size_t remaining = 0;
        do {
            remaining = 0;
            for (size_t th = 0; th < configs.size(); ++th) {
                const TestFn& testFn = std::get<0>(configs[th]);
                unsigned& thCount    = std::get<1>(configs[th]);
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


} // lockless

#endif // __lockess__test_h__
