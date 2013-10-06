/* snzi_para_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 06 Oct 2013
   FreeBSD-style copyright and disclaimer apply

   Parallel tests for snzi
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#define LOCKLESS_SNZI_DEBUG 1
#define LOCKLESS_CHECK_ABORT 1

#include "snzi.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>

using namespace std;
using namespace lockless;

template<size_t Nodes, size_t Arity>
void basicTest()
{
    enum {
        Iterations = 100,
        Threads = 4
    };

    cerr << fmtTitle(format("nodes=%lld, arity=%lld", Nodes, Arity)) << endl;

    Snzi<Nodes, Arity> snzi;
    auto& log = NullLog;

    auto doThread = [&] (unsigned) {
        for (size_t it = 0; it < Iterations; ++it) {
            snzi.inc();
            locklessCheck(snzi.test(), log);
            snzi.dec();
        }
    };

    ParallelTest test;
    test.add(doThread, Threads);
    test.run();

    locklessCheck(!snzi.test(), log);
}

BOOST_AUTO_TEST_CASE(basic)
{
    basicTest<1, 1>();
    basicTest<2, 2>();
    basicTest<2, 8>();
    basicTest<8, 2>();
    basicTest<8, 8>();
}
