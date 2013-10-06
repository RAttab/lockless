/* snzi.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 12 Feb 2013
   FreeBSD-style copyright and disclaimer apply

   Scalable non-zero indicator implementation.

   The idea here is to minimize contention of a cache line containing a counter
   by distributing the atomic ops over a tree.

   \todo Add proper credit to the original Sun/Oracle paper.

   \todo Can't quite figure out how the fast path from the paper works. CAS on
   the root at the begining of an op only really make sense if the intruction
   set doesn't contain an atomic inc (x86 does so...). Otherwise you already
   payed the cost of bringing the cache line to your processor so you may as
   well do the increment and get it over with.

   Maybe what they meant is to CAS the root until you fail one too many times.
   At which point you start working on the tree ignoring the fast-path. Problem
   with this is that its very vulnerable to load-spikes.

   I think one way to dynamically adjust to the current contention level is to
   make nodeForThread() smarter. The idea is that the lower you are in the tree
   the more contention you've experienced so thread would starts working on the
   root and as their CAS fail or succeed, they get pushed up or down the tree.
   Could be tricky to get right and we'd need thread local variables which is
   always a pain..

*/

#ifndef __lockless__snzi_h__
#define __lockless__snzi_h__

#include "utils.h"
#include "log.h"
#include "check.h"
#include "arch.h"

#include <atomic>

namespace lockless {


/******************************************************************************/
/* SNZI                                                                       */
/******************************************************************************/

template<size_t Nodes, size_t Arity = 2>
struct Snzi
{
    locklessStaticAssert(Nodes > 0);
    locklessStaticAssert(Arity > 1);

    Snzi()
    {
        for (size_t i = 0; i < tree.size(); ++i)
            tree[i].store(0);
    }

    bool test() const { return tree[0].load(); }

    /** Returns true if the state changed from 0 to 1. */
    bool inc() { return inc(nodeForThread()); }

    /** Returns true if the state changed from 1 to 0. */
    bool dec() { return dec(nodeForThread()); }

private:

    size_t nodeForThread() const { return threadId() % tree.size(); }

    bool inc(size_t node) locklessNeverInline
    {
        if (!node) return tree[0].fetch_add(1) == 0;

        size_t value = tree[node].load();
        size_t parent = node / Arity;

        while (true) {
            if (value > 1) {
                if (!tree[node].compare_exchange_weak(value, value + 1))
                    continue;
                return false;
            }

            if (!value) {
                if (!tree[node].compare_exchange_weak(value, 1))
                    continue;
                value = 1;
            }

            locklessCheckEq(value, size_t(1), NullLog);

            bool shifted = inc(parent);

            if (tree[node].compare_exchange_strong(value, 2))
                return shifted;

            dec(parent);
        }
    }

    bool dec(size_t node) locklessNeverInline
    {
        if (!node) return tree[0].fetch_sub(1) == 1;

        size_t value = tree[node].load();
        size_t parent = node / Arity;

        while (true) {
            locklessCheckGe(value, size_t(2), NullLog);

            if (value > 2) {
                if (!tree[node].compare_exchange_weak(value, value - 1))
                    continue;
                return false;
            }

            if (!tree[node].compare_exchange_weak(value, 0))
                continue;

            return dec(parent);
        }

        return false;
    }

    /** Gcc drops attributes in template args due to them not being part of name
        mangling so we have to create a new type instead to get our alignment
        attribute in.
    */
    struct locklessCacheAligned Counter : public std::atomic<size_t> {};
    std::array<Counter, Nodes> tree;
};


/******************************************************************************/
/* NULL SNZI                                                                  */
/******************************************************************************/

template<size_t Arity>
struct Snzi<1, Arity>
{
    Snzi() : counter(0) {}

    bool test() const { return counter.load(); }

    /** Returns true if the state changed from 0 to 1. */
    bool inc() { return counter.fetch_add(1) == 0; }

    /** Returns true if the state changed from 1 to 0. */
    bool dec() { return counter.fetch_sub(1) == 1; }

private:
    std::atomic<size_t> counter;
};

typedef Snzi<1,1> NullSnzi;

} // lockless

#endif // __lockless__snzi_h__
