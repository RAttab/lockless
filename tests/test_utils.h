/** test.h                                 -*- C++ -*-
    Rémi Attab (remi.attab@gmail.com), 24 Dec 2012
    FreeBSD-style copyright and disclaimer apply

    Testing framework for multithreaded algorithms

*/

#ifndef __lockess__test_h__
#define __lockess__test_h__

#include "utils.h"
#include "check.h"

#include <functional>
#include <algorithm>
#include <future>
#include <thread>
#include <random>
#include <array>


namespace lockless {


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


/******************************************************************************/
/* PREDICATES                                                                 */
/******************************************************************************/

template<typename LogT>
void checkPow2(size_t val, LogT& log, const CheckContext& ctx)
{
    locklessCheckOpCtx(==, val & (val - 1), 0ULL, log, ctx);
}

template<typename LogT>
void checkAlign(size_t val, size_t align, LogT& log, const CheckContext& ctx)
{
    checkPow2(align, log, ctx);
    locklessCheckOpCtx(==, val & (align - 1), 0ULL, log, ctx);
}

template<typename LogT>
void checkMem(
        void* block, size_t size, uint8_t value,
        LogT& log, const CheckContext& ctx)
{
    uint8_t* pBlock = reinterpret_cast<uint8_t*>(block);
    auto pred = [=] (uint8_t val) { return val == value; };
    locklessCheckCtx(std::all_of(pBlock, pBlock + size, pred), log, ctx);
}


} // lockless

#endif // __lockess__test_h__
