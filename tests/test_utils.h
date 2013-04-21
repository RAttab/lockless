/** test.h                                 -*- C++ -*-
    Rémi Attab (remi.attab@gmail.com), 24 Dec 2012
    FreeBSD-style copyright and disclaimer apply

    Testing framework for multithreaded algorithms

*/

#ifndef __lockess__test_h__
#define __lockess__test_h__

#include <functional>
#include <future>
#include <thread>
#include <random>
#include <array>


namespace lockless {

/******************************************************************************/
/* FORMAT UTILS                                                               */
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

std::string fmtTitle(const std::string& title, char fill = '-')
{
    std::array<char, 80> buffer;
    std::string filler(80 - title.size() - 4, fill);

    snprintf(buffer.data(), buffer.size(), "[ %s ]%s",
            title.c_str(), filler.c_str());
    return std::string(buffer.data());
}

/******************************************************************************/
/* PARALLEL TEST                                                              */
/******************************************************************************/

struct ParallelTest
{
    typedef std::function<void(unsigned)> TestFn;

    void add(const TestFn& fn, unsigned thCount)
    {
        configs.push_back(std::make_pair(fn, thCount));
    }

    void run()
    {
        std::vector< std::vector< std::future<void> > > futures;
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

                std::packaged_task<void()> task(std::bind(testFn, thCount));
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

                    futures[i][j].get();
                    processed++;
                }
            }

            if (!processed)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));

        } while (remaining > 0);
    }

    std::vector< std::pair<TestFn, unsigned> > configs;
};


/******************************************************************************/
/* RANDOM STRING                                                              */
/******************************************************************************/

template<typename Engine>
std::string randomString(size_t length, Engine& engine)
{
    const std::string source =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789";

    std::uniform_int_distribution<unsigned> dist(0, source.size());

    std::string output;
    while (output.size() < length) output += source[dist(engine)];
    return output;
}


} // lockless

#endif // __lockess__test_h__
