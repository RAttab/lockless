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


void checkPair(const std::pair<bool, size_t>& r)
{
    BOOST_CHECK_EQUAL(r.first, false);
    BOOST_CHECK_EQUAL(r.second, size_t());
}

void checkPair(const std::pair<bool, size_t>& r, size_t exp)
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
        // cerr << fmtTitle(to_string(i)) << endl;

        checkPair(map.find(i));
        checkPair(map.remove(i));
        BOOST_CHECK(!map.compareExchange(i, i, i*i));

        // logToStream(map.log);
    }

    // logToStream(map.log);

    cerr << fmtTitle("insert", '=') << endl;

    for (size_t i = 0; i < Size; ++i) {
        // cerr << fmtTitle(to_string(i)) << endl;

        BOOST_CHECK(map.insert(i, i));
        BOOST_CHECK(!map.insert(i, i+1));
        BOOST_CHECK_EQUAL(map.size(), i + 1);

        checkPair(map.find(i), i);

        // logToStream(map.log);
    }

    size_t capacity = map.capacity();

    // logToStream(map.log);

    cerr << fmtTitle("compareExchange", '=') << endl;

    for (size_t i = 0; i < Size; ++i) {
        // cerr << fmtTitle(to_string(i)) << endl;

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

        // logToStream(map.log);
    }

    BOOST_CHECK_EQUAL(map.size(), Size);
    BOOST_CHECK_EQUAL(map.capacity(), capacity);

    // logToStream(map.log);

    cerr << fmtTitle("remove", '=') << endl;

    for (size_t i = 0; i < Size; ++i) {
        // cerr << fmtTitle(to_string(i)) << endl;
        checkPair(map.remove(i), i);
        checkPair(map.remove(i));
        // logToStream(map.log);
    }

    cerr << fmtTitle("check", '=') << endl;

    for (size_t i = 0; i < Size; ++i) {
        // cerr << fmtTitle(to_string(i)) << endl;
        checkPair(map.find(i));
        checkPair(map.remove(i));

        size_t exp;
        BOOST_CHECK(!map.compareExchange(i, exp = i, i+1));
        BOOST_CHECK_EQUAL(exp, i);

        // logToStream(map.log);
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

BOOST_AUTO_TEST_CASE(fuzz_test)
{
    enum { Iterations = 2000 };
    Map<size_t, size_t> map;

    typedef MagicValue<size_t> Magic;

    std::vector< std::pair<size_t, size_t> > keys;

    mt19937_64 engine;
    uniform_int_distribution<size_t> actionRnd(0, 10);
    uniform_int_distribution<size_t> keyRnd(0, -1);

    cerr << fmtTitle("fuzz", '=') << endl;

    for (size_t i = 0; i < Iterations; ++i) {
        unsigned action = actionRnd(engine);

        // logToStream(map.log);
        // cerr << fmtTitle(to_string(i)) << endl;

        if (keys.empty() || action < 6) {
            size_t key = details::clearMarks<Magic>(keyRnd(engine));
            size_t value = details::clearMarks<Magic>(keyRnd(engine));

            BOOST_CHECK(map.insert(key, value));
            BOOST_CHECK(!map.insert(key, value));
            keys.push_back(make_pair(key, value));
        }

        else if (action < 7) {
            size_t index = keyRnd(engine) % keys.size();
            size_t newValue = details::clearMarks<Magic>(keyRnd(engine));
            auto& kv = keys[index];

            BOOST_CHECK(map.compareExchange(kv.first, kv.second, newValue));
            BOOST_CHECK(!map.compareExchange(kv.first, kv.second, newValue));
            BOOST_CHECK_EQUAL(kv.second, newValue);

            keys[index].second = newValue;
        }

        else {
            size_t index = keyRnd(engine) % keys.size();
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
