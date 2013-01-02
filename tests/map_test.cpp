/* map_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 01 Jan 2013
   FreeBSD-style copyright and disclaimer apply

   Tests for Map.

*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

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

    for (uint64_t i = 0; i < Size; ++i) {
        checkPair(map.find(i));
        checkPair(map.remove(i));
    }

    for (uint64_t i = 0; i < Size; ++i) {
        BOOST_CHECK(map.insert(i, i));
        BOOST_CHECK(!map.insert(i, i*i));
        BOOST_CHECK_EQUAL(map.size(), i);

        checkPair(map.find(i), i);
    }

    uint64_t capacity = map.capacity();

    for (uint64_t i = 0; i < Size; ++i) {
        uint64_t exp;

        BOOST_CHECK(!map.compareExchange(i, exp = i*i, i));
        BOOST_CHECK_EQUAL(exp, i);
        BOOST_CHECK( map.compareExchange(i, exp = i, i*i));

        checkPair(map.find(i), i*i);

        BOOST_CHECK(!map.compareExchange(i, exp = i, i*i));
        BOOST_CHECK_EQUAL(exp, i*i);
        BOOST_CHECK( map.compareExchange(i, exp = i*i, i));

        checkPair(map.find(i), i);
        BOOST_CHECK(!map.insert(i, i*i));

    }

    BOOST_CHECK_EQUAL(map.size(), Size);
    BOOST_CHECK_EQUAL(map.capacity(), capacity);

    for (uint64_t i = 0; i < Size; ++i) {
        checkPair(map.remove(i), i);
        checkPair(map.remove(i));
        checkPair(map.find(i));

    }
}
