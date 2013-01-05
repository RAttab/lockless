/* map_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 01 Jan 2013
   FreeBSD-style copyright and disclaimer apply

   Tests for Map.

*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#define LOCKLESS_MAP_DEBUG 1

#include "map.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>
#include <random>
#include <vector>

using namespace std;
using namespace lockless;


template<typename T>
void checkPair(const std::pair<bool, T>& r)
{
    BOOST_CHECK_EQUAL(r.first, false);
    BOOST_CHECK_EQUAL(r.second, T());
}

template<typename T>
void checkPair(const std::pair<bool, T>& r, T exp)
{
    BOOST_CHECK_EQUAL(r.first, true);
    BOOST_CHECK_EQUAL(r.second, exp);
}


BOOST_AUTO_TEST_CASE(resize_test)
{
    cerr << fmtTitle("init", '=') << endl;
    Map<size_t, size_t> map;
    // logToStream(map.log);

    cerr << fmtTitle("resize 0", '=') << endl;
    map.resize(1ULL << 6);
    // logToStream(map.log);

    cerr << fmtTitle("resize 1", '=') << endl;
    map.resize(1ULL << 7);
    // logToStream(map.log);

    cerr << fmtTitle("resize 2", '=') << endl;
    map.resize(1ULL << 8);
    // logToStream(map.log);
}


BOOST_AUTO_TEST_CASE(basic_test)
{
    enum { Size = 100 };

    Map<size_t, size_t> map;

    BOOST_CHECK_EQUAL(map.size(), 0);

    cerr << fmtTitle("fail find", '=') << endl;

    for (size_t i = 0; i < Size; ++i) {
        checkPair(map.find(i));
        checkPair(map.remove(i));
        BOOST_CHECK(!map.compareExchange(i, i, i*i));
    }

    cerr << fmtTitle("insert", '=') << endl;

    for (size_t i = 0; i < Size; ++i) {
        BOOST_CHECK(map.insert(i, i));
        BOOST_CHECK(!map.insert(i, i+1));
        BOOST_CHECK_EQUAL(map.size(), i + 1);

        checkPair(map.find(i), i);
    }

    size_t capacity = map.capacity();

    cerr << fmtTitle("compareExchange", '=') << endl;

    for (size_t i = 0; i < Size; ++i) {
        size_t exp;

        BOOST_CHECK(!map.compareExchange(i, exp = i+1, i));
        BOOST_CHECK_EQUAL(exp, i);
        BOOST_CHECK( map.compareExchange(i, exp = i, i+1));

        checkPair(map.find(i), i+1);

        BOOST_CHECK(!map.compareExchange(i, exp = i, i+1));
        BOOST_CHECK_EQUAL(exp, i+1);
        BOOST_CHECK( map.compareExchange(i, exp = i+1, i));

        checkPair(map.find(i), i);
        BOOST_CHECK(!map.insert(i, i+1));
    }

    BOOST_CHECK_EQUAL(map.size(), Size);
    BOOST_CHECK_EQUAL(map.capacity(), capacity);

    cerr << fmtTitle("remove", '=') << endl;

    for (size_t i = 0; i < Size; ++i) {
        checkPair(map.remove(i), i);
        checkPair(map.remove(i));
    }

    cerr << fmtTitle("check", '=') << endl;

    for (size_t i = 0; i < Size; ++i) {
        checkPair(map.find(i));
        checkPair(map.remove(i));

        size_t exp;
        BOOST_CHECK(!map.compareExchange(i, exp = i, i+1));
        BOOST_CHECK_EQUAL(exp, i);
    }
}

/* If the TombstoneThreshold is greater then 1 then this test case will exhaust
   memory. The first series of insert ensures that all the following removes
   will have a full probe window thanks to std::hash evaluating to identity for
   integers.

   When we do the first remove, we tombstone the bucket which is not all that
   interesting. The second remove, will find the tombstone and fail to find the
   key in the probe window. This will trigger a resize and if the threshold is
   greater then 1 then the table will double. Rinse and repeat until we run out
   of address space. That or tcmalloc gets pissy.

   If the threshold is 1 then this will trigger a cleanup instead and the table
   size remains constant.
 */
BOOST_AUTO_TEST_CASE(erratic_remove_test)
{
    enum { Size = 100 };
    Map<size_t, size_t> map;

    cerr << fmtTitle("erratic-remove", '=') << endl;

    for (size_t i = 0; i < Size; ++i)
        map.insert(i, i);

    for (size_t i = 0; i < Size; ++i) {
        map.remove(i);
        map.remove(i);
    }
}

template<typename Key, typename Value, typename Engine>
void fuzzTest(const function< pair<Key, Value>() >& gen, Engine& rng)
{
    enum { Iterations = 2000 };

    Map<Key, Value> map;
    vector< pair<Key, Value> > keys;

    uniform_int_distribution<size_t> actionRnd(0, 10);

    auto indexRnd = [&] {
        return uniform_int_distribution<size_t>(0, keys.size() - 1)(rng);
    };

    cerr << fmtTitle("fuzz", '=') << endl;

    for (size_t i = 0; i < Iterations; ++i) {
        unsigned action = actionRnd(rng);

        if (keys.empty() || action < 5) {
            auto ret = gen();
            BOOST_CHECK(map.insert(ret.first, ret.second));
            BOOST_CHECK(!map.insert(ret.first, ret.second));
            keys.push_back(ret);
        }

        else if (action < 7) {
            Value newValue = gen().second;
            size_t index = indexRnd();
            auto& kv = keys[index];

            BOOST_CHECK(map.compareExchange(kv.first, kv.second, newValue));
            BOOST_CHECK(!map.compareExchange(kv.first, kv.second, newValue));
            BOOST_CHECK_EQUAL(kv.second, newValue);

            keys[index].second = newValue;
        }

        else {
            size_t index = indexRnd();
            auto& kv = keys[index];

            checkPair(map.remove(kv.first), kv.second);
            checkPair(map.remove(kv.first));
            keys.erase(keys.begin() + index);
        }

        BOOST_CHECK_EQUAL(map.size(), keys.size());

        for (size_t j = 0; j < keys.size(); ++j) {
            auto& kv = keys[j];
            checkPair(map.find(kv.first), kv.second);
        }

    }

    cerr << "Final Size=" << map.size() << endl;

    for (size_t i = 0; i < keys.size(); ++i) {
        auto& kv = keys[i];
        checkPair(map.remove(kv.first), kv.second);
    }
}

BOOST_AUTO_TEST_CASE(int_fuzz_test)
{
    static mt19937_64 rng;
    uniform_int_distribution<unsigned> dist(0, -1);

    auto gen = [&] {
        typedef MagicValue<size_t> Magic;
        return make_pair(
                details::clearMarks<Magic>(dist(rng)),
                details::clearMarks<Magic>(dist(rng)));
    };

    fuzzTest<size_t, size_t>(gen, rng);
}

BOOST_AUTO_TEST_CASE(string_fuzz_test)
{
    static mt19937_64 rng;
    uniform_int_distribution<unsigned> dist(0, 256);

    auto gen = [&] {
        return make_pair(
                randomString(dist(rng), rng),
                randomString(dist(rng), rng));
    };

    fuzzTest<string, string>(gen, rng);
}
