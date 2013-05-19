/* list_seq_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 07 Apr 2013
   FreeBSD-style copyright and disclaimer apply

   Tests for the List linked list.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#define LOCKLESS_LIST_DEBUG 1
#define LOCKLESS_CHECK_ABORT 1

#include "list.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>

using namespace std;
using namespace lockless;

BOOST_AUTO_TEST_CASE(test_node)
{
    cerr << fmtTitle("node", '=') << endl;

    typedef ListNode<size_t> Node;

    Node(); // default constructor.

    Node a(size_t(10));
    locklessCheckEq(a, size_t(10), NullLog);
    locklessCheckEq(a.value, size_t(10), NullLog);
    locklessCheckEq(a.next(), nullptr, NullLog);

    Node b(20);
    locklessCheckEq(b, size_t(20), NullLog);
    locklessCheckEq(b.value, size_t(20), NullLog);
    locklessCheckEq(b.next(), nullptr, NullLog);

    Node* exp = nullptr;
    locklessCheck(a.compare_exchange_next(exp, &b), NullLog);
    locklessCheckEq(exp, nullptr, NullLog);

    locklessCheck(!a.compare_exchange_next(exp, &b), NullLog);
    locklessCheckEq(exp, &b, NullLog);

    Node c{Node(a)}; // copy & move constructor.
    locklessCheckEq(c, size_t(10), NullLog);
    locklessCheckEq(c.value, size_t(10), NullLog);
    locklessCheckEq(c.next(), nullptr, NullLog);

    Node* pNil = b.mark();
    locklessCheck(b.isMarked(), NullLog);
    locklessCheckEq(pNil, nullptr, NullLog);
    locklessCheckEq(b.next(), pNil, NullLog);

    b.reset();
    locklessCheck(!b.isMarked(), NullLog);
    locklessCheckEq(b.next(), nullptr, NullLog);

    Node* pB = a.mark();
    locklessCheck(a.isMarked(), NullLog);
    locklessCheckEq(pB, &b, NullLog);
    locklessCheckEq(a.next(), pB, NullLog);

    a.reset();
    locklessCheck(!a.isMarked(), NullLog);
    locklessCheckEq(a.next(), nullptr, NullLog);
}



struct ListFixture
{
    typedef ListNode<size_t> Node;

    ListFixture() : size(10), log(list.log)
    {
        for (size_t i = 0; i < size; ++i) {
            Node* node = new Node(i);
            list.push(node);
            locklessCheckEq(list.head.load(), node, log);
        }
    }

    const size_t size;

    List<size_t> list;
    decltype(list.log)& log;
};


BOOST_FIXTURE_TEST_CASE(test_push_pop, ListFixture)
{
    cerr << fmtTitle("push pop", '=') << endl;

    // fixture does the pushing.

    for (size_t i = 0; i < 10; ++i) {
        Node* node = list.pop();
        locklessCheckEq(*node, 9-i, log);
        locklessCheckEq(node->next(), list.head.load(), log);
        delete node;
    }
}

BOOST_FIXTURE_TEST_CASE(test_pop_marked, ListFixture)
{
    cerr << fmtTitle("pop marked", '=') << endl;

    cerr << fmtTitle("mark") << endl;

    Node* node = list.head;
    while (node && node->next()) {
        node->mark();
        locklessCheck(node->isMarked(), list.log);
        node = node->next()->next();
    }

    cerr << fmtTitle("pop") << endl;
    size_t unmarked = 0;

    for (size_t i = 0; i < size; ++i) {
        node = list.popMarked();

        if (node == nullptr) {
            locklessCheckEq(node, nullptr, log);
            list.head.load()->mark();
            unmarked++;
            i--;
            continue;
        }

        locklessCheckEq(node->next(), list.head.load(), log);
        locklessCheck(node, log);
        locklessCheckEq(*node, size - i - 1, log);
        delete node;
    }

    locklessCheck(list.empty(), log);
    locklessCheckEq(unmarked, size / 2, log);
}

BOOST_FIXTURE_TEST_CASE(test_remove, ListFixture)
{
    cerr << fmtTitle("remove", '=') << endl;

    cerr << fmtTitle("skip") << endl;

    Node* node = list.head;
    size_t sum = 0;
    while (node && node->next()) {
        locklessCheck(list.remove(node), log);
        Node* next = node->next();

        sum += *node;
        delete node;
        node = next->next();
    }

    cerr << fmtTitle("all") << endl;

    node = list.head;
    while (node) {
        locklessCheck(list.remove(node), log);
        Node* next = node->next();

        sum += *node;
        delete node;
        node = next;
    }

    locklessCheckEq(sum, ((size-1) * size) / 2, log);
}
