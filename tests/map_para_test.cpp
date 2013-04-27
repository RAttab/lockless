/* map_para_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 05 Jan 2013
   FreeBSD-style copyright and disclaimer apply

   Parallel tests for Map.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#define LOCKLESS_MAP_DEBUG 1
#define LOCKLESS_CHECK_ABORT 1


#include "map.h"
#include "check.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>
#include <random>
#include <vector>
#include <set>


using namespace std;
using namespace lockless;


template<typename Key, typename Value>
void noContentionTest(const function< pair<Key, Value>() >& gen)
{
    enum {
        Threads = 8,
        Iterations = 100,
        Keys = 100,
    };

    cerr << fmtTitle("para", '=') << endl;

    std::set<Key> keys;
    array< array< pair<Key, Value>, Keys>, Threads> dataset;

    for (size_t th = 0; th < dataset.size(); ++th) {
        for (size_t i = 0; i < Keys; ++i) {
            do {
                dataset[th][i] = gen();
            } while (!keys.insert(dataset[th][i].first).second);
        }
    }

    Map<Key, Value> map;
    auto log = map.allLogs();

    auto doThread = [&] (const unsigned id) {
        for (size_t it = 0; it < Iterations; ++it) {

            for (size_t i = 0; i < Keys; ++i) {
                auto& kv = dataset[id][i];
                checkPair(map.find(kv.first), log, locklessCtx());
            }

            for (size_t i = 0; i < Keys; ++i) {
                auto& kv = dataset[id][i];
                locklessCheck(map.insert(kv.first, kv.second), log);
            }

            for (size_t i = 0; i < Keys; ++i) {
                auto& kv = dataset[id][i];
                checkPair(map.find(kv.first), kv.second, log, locklessCtx());
            }

            for (size_t i = 0; i < Keys; ++i) {
                auto& kv = dataset[id][i];
                Value newValue;
                locklessCheck(map.compareExchange(kv.first, kv.second, newValue), log);
                locklessCheck(map.compareExchange(kv.first, newValue, kv.second), log);
            }

            for (size_t i = 0; i < Keys; ++i) {
                auto& kv = dataset[id][i];
                checkPair(map.find(kv.first), kv.second, log, locklessCtx());
            }

            for (size_t i = 0; i < Keys; ++i) {
                auto& kv = dataset[id][i];
                checkPair(map.remove(kv.first), kv.second, log, locklessCtx());
            }
        }
    };

    ParallelTest test;
    test.add(doThread, Threads);
    test.run();

    locklessCheckEq(map.size(), 0ULL, log);
    cerr << "capacity=" << map.capacity() << endl;
}


BOOST_AUTO_TEST_CASE(intNoContentionTest)
{
    static mt19937_64 rng;
    uniform_int_distribution<unsigned> dist(0, -1);

    auto gen = [&] {
        typedef MagicValue<size_t> Magic;
        return make_pair(
                details::clearMarks<Magic>(dist(rng)),
                details::clearMarks<Magic>(dist(rng)));
    };

    noContentionTest<size_t, size_t>(gen);
}

BOOST_AUTO_TEST_CASE(strNoContentionTest)
{
    static mt19937_64 rng;
    uniform_int_distribution<unsigned> dist(0, 256);

    auto gen = [&] {
        return make_pair(
                randomString(dist(rng), rng),
                randomString(dist(rng), rng));
    };

    noContentionTest<string, string>(gen);
}
