/* snzi_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 01 Sep 2013
   FreeBSD-style copyright and disclaimer apply

   Sequential tests for Snzi
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "snzi.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>

using namespace std;
using namespace lockless;

template<size_t Nodes, size_t Arity>
void basicTest()
{
    cerr << endl
        << fmtTitle(format("nodes=%lld, arity=%lld", Nodes, Arity))
        << endl;

    Snzi<Nodes, Arity> snzi;
    auto& log = NullLog;

    for (size_t it = 0; it < 10; ++it) {
        locklessCheck(!snzi.test(), log);

        locklessCheck(snzi.inc(), log);
        locklessCheck(snzi.test(), log);

        for (size_t i = 0; i < 10; ++i) locklessCheck(!snzi.inc(), log);
        locklessCheck(snzi.test(), log);

        for (size_t i = 0; i < 10; ++i) locklessCheck(!snzi.dec(), log);
        locklessCheck(snzi.test(), log);

        locklessCheck(snzi.dec(), log);
        locklessCheck(!snzi.test(), log);

        for (size_t i = 0; i < 10; ++i) {
            locklessCheck(snzi.inc(), log);
            locklessCheck(snzi.test(), log);
            locklessCheck(snzi.dec(), log);
            locklessCheck(!snzi.test(), log);
        }
    }
}

BOOST_AUTO_TEST_CASE(basics)
{
    basicTest<1, 1>();
    basicTest<2, 2>();
    basicTest<8, 2>();
    basicTest<2, 8>();
    basicTest<8, 8>();
}
