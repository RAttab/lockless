/* queue_para_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 16 Mar 2013
   FreeBSD-style copyright and disclaimer apply

   Parallel tests for the unbounded queue.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#define LOCKLESS_QUEUE_DEBUG 1

#include "queue.h"
#include "check.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>
#include <array>
#include <algorithm>

using namespace std;
using namespace lockless;

BOOST_AUTO_TEST_CASE(pubSubTest)
{
    enum {
        PubThreads = 4,
        SubThreads = 4,
        Values = 10000,
        TotalValues = Values * PubThreads
    };

    cerr << fmtTitle("pubSub", '=') << endl;

    mt19937_64 rng;
    uniform_int_distribution<size_t> valueDist(0, -1);

    atomic<unsigned> pubId(0);
    atomic<unsigned> pubDone(0);
    array<size_t, TotalValues> pubValues;
    for (size_t& value: pubValues) value = valueDist(rng);

    atomic<unsigned> subIndex(0);
    array<size_t, TotalValues> subValues;
    for (size_t& value: subValues) value = 0;


    Queue<size_t> queue;
    auto log = queue.allLogs();

    auto pubThread = [&] (unsigned) {
        const unsigned id = pubId.fetch_add(1);

        for (size_t i = 0; i < Values; ++i)
            queue.push(pubValues[id * Values + i]);

        pubDone++;
    };

    auto subThread = [&] (unsigned) {
        while (true) {
            auto ret = queue.pop();

            if (!ret.first && pubDone.load() == PubThreads) break;
            if (!ret.first) continue; // yield?

            subValues[subIndex.fetch_add(1)] = ret.second;
        }
    };

    ParallelTest test;
    test.add(pubThread, PubThreads);
    test.add(subThread, SubThreads);
    test.run();

    sort(pubValues.begin(), pubValues.end());
    sort(subValues.begin(), subValues.end());

    bool eq = std::equal(pubValues.begin(), pubValues.end(), subValues.begin());
    if (!eq) {
        cerr << "Values:" << endl;
        for (size_t i = 0; i < TotalValues; ++i) {
            cerr << "\t" << pubValues[i] << " - " << subValues[i]
                << " -> " << (pubValues[i] == subValues[i])
                << endl;
        }

        vector<size_t> diff;

        cerr << "Lost Values: " << endl;
        set_difference(
                pubValues.begin(), pubValues.end(),
                subValues.begin(), subValues.end(),
                back_inserter(diff));


        cerr << "Extra Values: " << endl;
        diff.clear();
        set_difference(
                subValues.begin(), subValues.end(),
                pubValues.begin(), pubValues.end(),
                back_inserter(diff));

    }

    locklessCheck(eq, log);
}
