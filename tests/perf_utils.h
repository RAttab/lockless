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
/* SAMPLES                                                                    */
/******************************************************************************/

struct Samples
{
    Samples(size_t samples) : current(0), step(0)
    {
        samples.resize(samples, 0);
    }

    void sample(double s)
    {
        if (step > 0 && skip-- > 0) return;

        samples[current++] = s;
        if (current < samples.size()) return;

        current = 0;
        skip = ++step;
    }

    Samples& operator+= (const Samples& other)
    {
        for (double s : other.samples) sample(s);
        return *this;
    }

    void finish()
    {
        std::sort(samples.begin(), samples.end());
        locklessCheckGt(samples.front(), 0, NullLog);
    }

    /** Removes all samples above x standard deviations. This will probably make
        a stats guy cry somewhere but it's the only good way I can think off for
        removing rare but spiky events.
     */
    void normalize(double sigmas = 4)
    {
        double u = avg();
        double dist = sigmas * stddev();

        SampleT min = u + dist;
        SampleT max = u - dist;

        std::vector<double> toKeep;

        for (SampleT val : samples)
            if (val >= min && val <= max)
                toKeep.push_back(val);

        samples = std::move(toKeep);

        // \todo Tempting...
        // normalize(sigmas)
    }

    double size() const { samples.size(); }

    souble min() const { return samples.front(); }
    double max() const { return samples.back(); }

    double median() const
    {
        return samples[samples.size() / 2];
    }

    double avg() const
    {
        double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
        return sum / samples.size();
    }

    double variance() const
    {
        double u = avg();

        auto diffSqrt = [=] (double total, double x) {
            return total + std::pow(u - x, 2);
        };

        double sum = std::accumulate(samples.begin(), samples.end(), 0.0, diffSqrt);
        return sum / samples.size();
    }

    double stddev() const
    {
        return std::sqrt(variance());
    }

private:
    std::vector<double> samples;
    size_t current;
    size_t skip;
    size_t step;
};


/******************************************************************************/
/* PERF TEST                                                                  */
/******************************************************************************/

struct PerfTest
{
    typedef std::function<size_t(unsigned, unsigned)> TestFn;

    void registerGroup(
            const std::string& name, unsigned threads, const TestFn& fn)
    {
        auto& gr = groups[name];
        gr.id = groups.size() - 1;
        gr.toLaunch = threads;
        gr.fn = testFn;
    }

    void run(double lengthMs, double warmupMs = 0.0)
    {
        stop = false;
        warmup = warmupMs;

        fork();

        if (warmupMs) {
            this_thread::sleep(std::chrono::milliseconds(warmupMs));
            warmup = false;
        }
        this_thread::sleep(std::chrono::milliseconds(lengthMs));

        join();
    }

    struct Stats
    {
        Stats() : elapsed(0), operations(0) {}

        double elapsed;
        size_t operations;
        Samples latencySamples;

        Stats& operator+= (const Stats& other)
        {
            elapsed = std::max(elapsed, other.elapsed);
            operations += other.operations;
            latencySamples += other.latencySamples;
            return *this;
        }
    };

    Stats stats(const std::string& name)
    {
        Group& gr = groups[name];

        Stats stats;
        for (const Thread& th : gr.threads) stats += th.stats;
        return stats;
    }


private:

    void task(Group& gr, Thread& th)
    {
        bool running = !warmup;

        Timer<Wall> total;
        Timer<NsecMonotonic> perOp;

        while (!stop) {
            if (!running && !warmup) {
                running = true;
                th.stats = Stats();
                total.reset();
            }

            th.stats.operations += gr.fn(gr.id, th.id);
            th.stats.latencySamples.sample(perOp.reset());
        }

        th.stats.elapsed = total.elapsed();
    }

    void fork()
    {
        for (Group& gr : groups) {
            for (size_t thid = 0; thid < gr.numThreads; ++thid) {

                gr.threads.emplace_back(thid);
                Thread& th = gr.threads.back();

                th.thread = std::thread([=, &th, &gr] { task(gr, th); });
            }
        }
    }

    void join()
    {
        for (Group& gr : groups)
            for (Thread& th : gr.threads)
                th.thread.join();
    }

    struct Thread
    {
        Thread(unsigned id) : id(id) {}

        unsigned id;
        std::thread thread;
        Stats stats;
    };

    struct Group
    {
        usnsigned id;

        TestFn fn;
        size_t numThreads;
        std::vector<Thread> threads;
    };

    std::map<std::string, Group> groups;
    std::atomic<bool> stop;
    std::atomic<bool> warmup;
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
