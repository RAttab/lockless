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

using namespace std;
using namespace lockless;


void checkPair(const std::pair<bool, uint64_t>& r)
{
    BOOST_CHECK_EQUAL(r.first, false);
    BOOST_CHECK_EQUAL(r.second, uint64_t());
}

void checkPair(const std::pair<bool, uint64_t>& r, uint64_t exp)
{
    BOOST_CHECK_EQUAL(r.first, true);
    BOOST_CHECK_EQUAL(r.second, exp);
}


BOOST_AUTO_TEST_CASE(basic_test)
{
    enum { Size = 10 };

    Map<uint64_t, uint64_t> map;

    BOOST_CHECK_EQUAL(map.size(), 0);

    cerr << fmtTitle("fail find", '=') << endl;

    for (uint64_t i = 0; i < Size; ++i) {
        cerr << fmtTitle(to_string(i)) << endl;

        checkPair(map.find(i));
        checkPair(map.remove(i));
        BOOST_CHECK(!map.compareExchange(i, i, i*i));

        logToStream(map.log);
    }

    logToStream(map.log);

    cerr << fmtTitle("insert", '=') << endl;

    for (uint64_t i = 0; i < Size; ++i) {
        cerr << fmtTitle(to_string(i)) << endl;

        BOOST_CHECK(map.insert(i, i));
        BOOST_CHECK(!map.insert(i, i+1));
        BOOST_CHECK_EQUAL(map.size(), i + 1);

        checkPair(map.find(i), i);

        logToStream(map.log);
    }

    uint64_t capacity = map.capacity();

    logToStream(map.log);

    cerr << fmtTitle("compareExchange", '=') << endl;

    for (uint64_t i = 0; i < Size; ++i) {
        cerr << fmtTitle(to_string(i)) << endl;

        uint64_t exp;

        BOOST_CHECK(!map.compareExchange(i, exp = i+1, i));
        BOOST_CHECK_EQUAL(exp, i);
        BOOST_CHECK( map.compareExchange(i, exp = i, i+1));

        checkPair(map.find(i), i+1);

        BOOST_CHECK(!map.compareExchange(i, exp = i, i+1));
        BOOST_CHECK_EQUAL(exp, i+1);
        BOOST_CHECK( map.compareExchange(i, exp = i+1, i));

        checkPair(map.find(i), i);
        BOOST_CHECK(!map.insert(i, i+1));

        logToStream(map.log);
    }

    BOOST_CHECK_EQUAL(map.size(), Size);
    BOOST_CHECK_EQUAL(map.capacity(), capacity);

    logToStream(map.log);

    cerr << fmtTitle("remove", '=') << endl;

    for (uint64_t i = 0; i < Size; ++i) {
        cerr << fmtTitle(to_string(i)) << endl;

        checkPair(map.remove(i), i);
        checkPair(map.remove(i));
        checkPair(map.find(i));

        logToStream(map.log);
    }
}
