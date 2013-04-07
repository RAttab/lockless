/* list_para_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 07 Apr 2013
   FreeBSD-style copyright and disclaimer apply

   Parallel tests for list.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#define LOCKLESS_LIST_DEBUG 1
#define LOCKLESS_CHECK_ABORT 1

#include "list.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>
#include <array>

using namespace std;
using namespace lockless;

BOOST_AUTO_TEST_CASE(test_pop_marked)
{
    enum {
        Threads = 4,
        Keys = 100000
    };

    cerr << fmtTitle("pop marked", '=') << endl;

    List<size_t> list;
    typedef decltype(list)::Node Node;
    auto& log = list.log;

    SignalCheck guard(log);

    array< array<Node, Keys>, Threads> nodes;
    atomic<size_t> done(0);

    auto doInsertThread = [&] (unsigned id) {
        for (size_t i = 0; i < Keys; ++i) {
            nodes[id][i] = Node(id * Keys + i);
            locklessCheck(!nodes[id][i].isMarked(), log);
            locklessCheckEq(nodes[id][i].next(), nullptr, log);

          restart:
            Node* node = list.head;
            while (node && node->isMarked()) node = node->next();

            if (!node) list.push(&nodes[id][i]);
            else {
                Node* oldNext = node->next();
                nodes[id][i].next(oldNext);
                if (node->compare_exchange_next(oldNext, &nodes[id][i]))
                    continue;

                nodes[id][i].reset();
                goto restart;
            }
        }
        done++;
    };

    auto doMarkThread = [&] (unsigned id) {
        while (done != Threads) {

            size_t i = id;
            Node* node = list.head;

            while (node != nullptr) {
                if (i++ % Threads == 0) node->mark();
                node = node->next();
            }
        }

        for (Node* node = list.head; node; node = node->next())
            node->mark();
    };

    atomic<size_t> popped(0);
    atomic<size_t> sum(0);

    auto doPopThread = [&] (unsigned) {
        size_t localSum = 0;
        size_t localPop = 0;

        while (done != Threads || list.head) {
            Node* node = list.popMarked();
            if (node == nullptr) continue;

            localPop++;
            localSum += *node;
            locklessCheck(node->isMarked(), log);
            *node = -1LL;
        }

        popped += localPop;
        sum += localSum;
    };

    ParallelTest test;
    test.add(doPopThread, Threads);
    test.add(doMarkThread, Threads);
    test.add(doInsertThread, Threads);
    test.run();

    size_t n = popped;
    locklessCheckEq(list.head.load(), nullptr, log);
    locklessCheckEq(n, size_t(Threads * Keys), log);
    locklessCheckEq(sum.load(), ((n-1) * n) / 2, log);

    for (size_t i = 0; i < Threads; ++i)
        for (size_t j = 0; j < Keys; ++j)
            locklessCheckEq(nodes[i][j], size_t(-1), log);
}

BOOST_AUTO_TEST_CASE(test_remove) {
    enum {
        Threads = 4,
        Keys = 100000
    };

    cerr << fmtTitle("remove", '=') << endl;

    List<size_t> list;
    typedef decltype(list)::Node Node;
    auto& log = list.log;

    SignalCheck guard(log);

    array< array<Node, Keys>, Threads> nodes;
    atomic<size_t> done(0);

    auto doInsertThread = [&] (const unsigned id) {
        for (size_t i = 0; i < Keys; ++i) {
            nodes[id][i] = Node(Keys * id + i);
            locklessCheckEq(nodes[id][i].next(), nullptr, log);

            list.push(&nodes[id][i]);
        }
        done++;
    };

    atomic<size_t> sum(0);
    atomic<size_t> removed(0);

    auto doRemoveThread = [&] (unsigned) {
        size_t localSum = 0;
        size_t localCount = 0;

        while (done != Threads || list.head) {
            Node* node = list.head;
            if (!node || !list.remove(node)) continue;

            localCount++;
            localSum += *node;

            locklessCheck(node->isMarked(), log);
            *node = -1;
        }

        sum += localSum;
        removed += localCount;
    };

    ParallelTest test;
    test.add(doRemoveThread, Threads);
    test.add(doInsertThread, Threads);
    test.run();

    size_t n = removed;
    locklessCheckEq(list.head.load(), nullptr, log);
    locklessCheckEq(n, size_t(Threads * Keys), log);
    locklessCheckEq(sum.load(), ((n-1) * n) / 2, log);

    for (size_t i = 0; i < Threads; ++i)
        for (size_t j = 0; j < Keys; ++j)
            locklessCheckEq(nodes[i][j], size_t(-1), log);
}
