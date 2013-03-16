/* queue_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 16 Mar 2013
   FreeBSD-style copyright and disclaimer apply

   Tests for the unbounded lock-free queue.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "queue.h"
#include "check.h"

#include <boost/test/unit_test.hpp>

using namespace std;
using namespace lockless;

BOOST_AUTO_TEST_CASE(basicTest)
{
    Queue<size_t> queue;
    auto log = queue.allLogs();

    for (size_t k = 0; k < 100; ++k) {
        checkPair(queue.peek(), log, locklessCtx());

        for (size_t i = 0; i < 100; ++i) {
            queue.push(i);
            checkPair(queue.peek(), size_t(0), log, locklessCtx());
        }

        for (size_t i = 0; i < 100; ++i) {
            checkPair(queue.peek(), i, log, locklessCtx());
            checkPair(queue.pop(), i, log, locklessCtx());
        }

        checkPair(queue.pop(), log, locklessCtx());
        checkPair(queue.peek(), log, locklessCtx());
    }

}
