/* ring_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 28 Sep 2013
   FreeBSD-style copyright and disclaimer apply

   Tests for the ring buffers.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#define LOCKLESS_CHECK_ABORT 1

#include <iostream>

#include "ring.h"
#include "check.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>

using namespace std;
using namespace lockless;


template<typename T, typename Ring>
void testCompile(T value)
{
    Ring ring;
    auto& log = NullLog;

    locklessCheck(ring.empty(), log);
    locklessCheckEq(ring.size(), size_t(0), log);
    locklessCheckEq(Ring::Size, ring.capacity(), log);
    locklessCheck(ring.push(value), log);
    locklessCheckEq(ring.pop(), value, log);
}

BOOST_AUTO_TEST_CASE(compile)
{
    cerr << fmtTitle("compile", '=') << endl;

    testCompile< size_t, RingQueueSRSW<size_t, 8> >(1);
    testCompile< size_t, RingQueueMRMW<size_t, 8> >(1);

    size_t value = 0;
    testCompile< size_t*, RingQueueSRSW<size_t*, 8> >(&value);
    testCompile< size_t*, RingQueueMRMW<size_t*, 8> >(&value);
}


template<typename Ring>
void testQueue(const std::string& title)
{
    cerr << fmtTitle(title + " - " + to_string(Ring::Size), '=') << endl;

    Ring ring;
    auto& log = NullLog;

    auto checkSize = [&] (size_t size) {
        locklessCheckEq(ring.size(), size, log);
        locklessCheckEq(ring.empty(), size == size_t(0), log);
    };

    for (size_t it = 0; it < 3; ++it) {
        cerr << fmtTitle(to_string(it)) << endl;

        checkSize(0);
        locklessCheck(!ring.pop(), log);

        for (size_t i = 0; i < Ring::Size * 2; ++i) {
            size_t value = i + 1;

            locklessCheck(ring.push(value), log);
            checkSize(1);

            locklessCheckEq(ring.pop(), value, log);
            checkSize(0);

            locklessCheckEq(ring.pop(), size_t(0), log);
            checkSize(0);
        }

        for (size_t i = 0; i < Ring::Size; ++i) {
            locklessCheck(ring.push(i + 1), log);
            checkSize(i + 1);
        }

        locklessCheck(!ring.push(1), log);

        for (size_t i = 0; i < Ring::Size; ++i) {
            locklessCheckEq(ring.pop(), i + 1, log);
            checkSize(Ring::Size - i - 1);
        }

        locklessCheck(!ring.pop(), log);
    }
}

BOOST_AUTO_TEST_CASE(queue)
{
    testQueue< RingQueueSRSW<size_t, 1> >("srsw");
    testQueue< RingQueueSRSW<size_t, 8> >("srsw");

    testQueue< RingQueueMRMW<size_t, 1> >("mrmw");
    testQueue< RingQueueMRMW<size_t, 8> >("mrmw");
}

template<size_t Size>
void testBuffer()
{
    typedef RingBuffer<size_t, Size> Ring;
    cerr << fmtTitle("buffer - " + to_string(Ring::Size), '=') << endl;

    Ring ring;
    auto& log = NullLog;

    auto checkSize = [&] (size_t size) {
        locklessCheckEq(ring.size(), size, log);
        locklessCheckEq(ring.empty(), size == size_t(0), log);
    };

    for (size_t it = 0; it < 3; ++it) {
        cerr << fmtTitle(to_string(it)) << endl;

        checkSize(0);
        locklessCheck(!ring.pop(), log);

        for (size_t i = 0; i < Ring::Size * 2; ++i) {
            size_t value = i + 1;

            ring.push(value);
            checkSize(1);

            locklessCheckEq(ring.pop(), value, log);
            checkSize(0);

            locklessCheckEq(ring.pop(), size_t(0), log);
            checkSize(0);
        }


        for (size_t i = 0; i < Ring::Size; ++i) {
            ring.push(i + 1);
            checkSize(i + 1);
        }

        for (size_t i = 0; i < Ring::Size; ++i) {
            locklessCheckEq(ring.pop(), i + 1, log);
            checkSize(Ring::Size - i - 1);
        }

        locklessCheck(!ring.pop(), log);


        for (size_t i = 0; i < Ring::Size * 2; ++i) {
            ring.push(i + 1);
            checkSize(i >= Ring::Size ? Ring::Size : i + 1);
        }

        for (size_t i = 0; i < Ring::Size; ++i) {
            locklessCheckEq(ring.pop(), Ring::Size + i + 1, log);
            checkSize(Ring::Size - i - 1);
        }

        locklessCheck(!ring.pop(), log);
    }
}

BOOST_AUTO_TEST_CASE(buffer)
{
    testBuffer<1>();
    testBuffer<8>();
}
