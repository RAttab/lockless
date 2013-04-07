/* list_seq_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 07 Apr 2013
   FreeBSD-style copyright and disclaimer apply

   Tests for the List linked list.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "list.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>

using namespace std;
using namespace lockless;

BOOST_AUTO_TEST_CASE(test_node)
{
    typedef ListNode<size_t> Node;

    Node(); // default constructor.

    Node a(size_t(10));
    BOOST_CHECK_EQUAL(a, size_t(10));
    BOOST_CHECK_EQUAL(a.get(), 10);
    BOOST_CHECK(a.next() == nullptr);

    Node b(20);
    BOOST_CHECK_EQUAL(b, size_t(20));
    BOOST_CHECK_EQUAL(b.get(), 20);
    BOOST_CHECK(b.next() == nullptr);

    Node* exp = nullptr;
    BOOST_CHECK(a.compare_exchange_next(exp, &b));
    BOOST_CHECK(exp == nullptr);

    BOOST_CHECK(!a.compare_exchange_next(exp, &b));
    BOOST_CHECK_EQUAL(exp, &b);

    Node c{Node(a)}; // copy & move constructor.
    BOOST_CHECK_EQUAL(c, size_t(10));
    BOOST_CHECK_EQUAL(c.get(), 10);
    BOOST_CHECK(c.next() == nullptr);
    
    Node* pNil = b.mark();
    BOOST_CHECK(b.isMarked());
    BOOST_CHECK(pNil == nullptr);
    BOOST_CHECK_EQUAL(b.next(), pNil);

    b.reset();
    BOOST_CHECK(!b.isMarked());
    BOOST_CHECK(b.next() == nullptr);

    Node* pB = a.mark();
    BOOST_CHECK(a.isMarked());
    BOOST_CHECK_EQUAL(pB, &b);
    BOOST_CHECK_EQUAL(a.next(), pB);

    a.reset();
    BOOST_CHECK(!a.isMarked());
    BOOST_CHECK(a.next() == nullptr);
}

BOOST_AUTO_TEST_CASE(test_list_basic)
{
    List<size_t> head;

    for (size_t i = 0; i < 10; ++i)
        head.push(new ListNode<size_t>(i));

    for (size_t i = 0; i < 10; ++i) {
        ListNode<size_t>* node = head.pop();
        BOOST_CHECK_EQUAL(*node, 9-i);
        delete node;
    }
}
