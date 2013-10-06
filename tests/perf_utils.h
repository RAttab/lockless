/* perf_utils.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 17 Mar 2013
   FreeBSD-style copyright and disclaimer apply

   Utilities for performance tests.
*/

#ifndef __lockless__perf_utils_h__
#define __lockless__perf_utils_h__

#include "tm.h"
#include "test_utils.h"

#include <map>
#include <cmath>


namespace lockless {



/******************************************************************************/
/* SAMPLES                                                                    */
/******************************************************************************/

struct Samples
{
    Samples(size_t size) : current(0), step(0)
    {
        samples.resize(size, 0);
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

        auto notZero = [] (double v) { return v != 0; };
        auto it = find_if(samples.begin(), samples.end(), notZero);
        samples.erase(samples.begin(), it);
    }

    /** Removes all samples above x standard deviations. This will probably make
        a stats guy cry somewhere but it's the only good way I can think off for
        removing rare but spiky events.
     */
    void normalize(double sigmas = 4)
    {
        double u = avg();
        double dist = sigmas * stddev();

        double min = u + dist;
        double max = u - dist;

        std::vector<double> toKeep;

        for (double val : samples)
            if (val >= min && val <= max)
                toKeep.push_back(val);

        samples = std::move(toKeep);

        // \todo Tempting...
        // normalize(sigmas)
    }

    size_t size() const { return samples.size(); }

    double min() const { return samples.front(); }
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

template<typename Context>
struct PerfTest
{
    typedef std::function<size_t(Context& ctx, unsigned)> TestFn;

    void add(const std::string& name, const TestFn& fn, unsigned threads)
    {
        auto& gr = groups[name];
        gr.numThreads = threads;
        gr.fn = fn;
    }

    struct Stats
    {
        Stats() :
            threadCount(1),
            elapsed(0),
            operations(0),
            latencySamples(1000)
        {}

        unsigned threadCount;
        double elapsed;
        size_t operations;
        Samples latencySamples;

        Stats& operator+= (const Stats& other)
        {
            threadCount += other.threadCount;
            elapsed = std::max(elapsed, other.elapsed);
            operations += other.operations;
            latencySamples += other.latencySamples;
            return *this;
        }

        std::string print(const std::string& title) const
        {
            double throughput = operations / elapsed / threadCount;

            return format(
                    "%-15s sec/ops=[ %s %s %s ] ops/sec=%s",
                    title.c_str(),
                    fmtElapsed(latencySamples.min()).c_str(),
                    fmtElapsed(latencySamples.median()).c_str(),
                    fmtElapsed(latencySamples.max()).c_str(),
                    fmtValue(throughput).c_str());
        }
    };

    Stats stats(const std::string& name)
    {
        Group& gr = groups[name];

        Stats stats;
        stats.threadCount = 0;
        for (const Thread& th : gr.threads) stats += th.stats;
        stats.latencySamples.finish();
        return stats;
    }

    std::string printStats(const std::string& name)
    {
        return stats(name).print(name);
    }

    void run(size_t lengthMs, size_t warmupMs = 0.0)
    {
        stop = false;
        warmup = warmupMs;

        Context ctx;
        fork(ctx);

        if (warmupMs) {
            sleep(warmupMs);
            warmup = false;
        }

        sleep(lengthMs);
        stop = true;

        join();
    }

private:

    struct Group;
    struct Thread;

    void task(Group& gr, Thread& th, Context& ctx)
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

            size_t ops = gr.fn(ctx, th.id);
            double latency = perOp.reset() / ops;

            th.stats.operations += ops;
            th.stats.latencySamples.sample(latency);
        }

        th.stats.elapsed = total.elapsed();
    }

    void fork(Context& ctx)
    {
        for (auto& entry : groups) {
            Group& gr = entry.second;
            gr.threads.reserve(gr.numThreads);

            for (size_t thid = 0; thid < gr.numThreads; ++thid) {
                gr.threads.emplace_back(thid);
                Thread& th = gr.threads.back();

                th.thread = std::thread([=, &th, &gr, &ctx] {
                            task(gr, th, ctx);
                        });
            }
        }
    }

    void join()
    {
        for (auto& gr : groups)
            for (Thread& th : gr.second.threads)
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
        TestFn fn;
        size_t numThreads;
        std::vector<Thread> threads;
    };

    std::map<std::string, Group> groups;
    std::atomic<bool> stop;
    std::atomic<bool> warmup;
};

} // lockless

#endif // __lockless__perf_utils_h__
