/* grcu_test.cc                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 28 Apr 2013
   FreeBSD-style copyright and disclaimer apply

   Tests for the global rcu implementation.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#define LOCKLESS_RCU_DEBUG 1
#define LOCKLESS_CHECK_ABORT 1

#include "grcu.h"
#include "check.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>
#include <iostream>
#include <random>
#include <vector>
#include <array>
#include <stack>
#include <map>

using namespace std;
using namespace lockless;

// This test has to be executed first before our thread initializes a gRcu node.
BOOST_AUTO_TEST_CASE(firstTest)
{
    cerr << fmtTitle("firstTest", '=') << endl;

    GlobalRcu rcu;
    rcu.gc();
    cerr << rcu.print() << endl;
}


// Quick test to make sure that the basics work properly.
BOOST_AUTO_TEST_CASE(smokeTest)
{
    cerr << fmtTitle("smokeTest", '=') << endl;

    GlobalRcu rcu;
    RcuGuard<GlobalRcu> guard(rcu);
    cerr << rcu.print() << endl;
}

// Run several checks to make sure that all RCU guarantees are respected when
// accessed from a single thread.
BOOST_AUTO_TEST_CASE(epochTest)
{
    cerr << fmtTitle("epochTest", '=') << endl;

    GlobalRcu rcu;
    auto log = rcu.log();

    size_t deferCount;
    auto deferFn = [&] { deferCount++; };

    for (size_t i = 0; i < 3; ++i) {

        deferCount = 0;
        cerr << fmtTitle("a, 0, 0") << endl;
        cerr << rcu.print() << endl;

        size_t e0 = rcu.enter();
        rcu.defer(deferFn);
        rcu.defer(deferFn);
        locklessCheckEq(rcu.enter(), e0, log);
        locklessCheckEq(deferCount, 0ULL, log);
        cerr << fmtTitle("b, 2, 0") << endl;
        cerr << rcu.print() << endl;

        rcu.exit(e0);
        rcu.gc();
        size_t e1 = rcu.enter();
        cerr << fmtTitle("c, 1, 1") << endl;
        cerr << rcu.print() << endl;
        locklessCheckNe(e1, e0, log);
        locklessCheckEq(deferCount, 0ULL, log);

        rcu.defer(deferFn);
        rcu.defer(deferFn);
        rcu.gc();
        locklessCheckEq(rcu.enter(), e1, log);
        locklessCheckEq(deferCount, 0ULL, log);
        cerr << fmtTitle("d, 1, 2") << endl;
        cerr << rcu.print() << endl;

        rcu.exit(e0);
        rcu.gc();
        size_t e2 = rcu.enter();
        rcu.defer(deferFn);
        cerr << fmtTitle("e, 1, 2") << endl;
        cerr << rcu.print() << endl;
        locklessCheckNe(e2, e0, log);
        locklessCheckNe(e2, e1, log);
        locklessCheckEq(deferCount, 2ULL, log);

        rcu.exit(e2);
        rcu.exit(e1);
        rcu.exit(e1);
        rcu.gc();
        locklessCheckEq(deferCount, 4ULL, log);
        rcu.gc();
        locklessCheckEq(deferCount, 5ULL, log);
        cerr << fmtTitle("f, 0, 0") << endl;
        cerr << rcu.print() << endl;
    }
}

BOOST_AUTO_TEST_CASE(deferOnExit)
{
    cerr << fmtTitle("deferOnExit", '=') << endl;

    size_t deferCount;
    auto deferFn = [&] { deferCount++; };

    for (size_t i = 0; i < 3; ++i) {
        deferCount = 0;
        {
            GlobalRcu rcu;
            rcu.defer(deferFn);
            rcu.defer(deferFn);
            rcu.gc();
            rcu.defer(deferFn);
            locklessCheckEq(deferCount, 0ULL, rcu.log());
        }
        locklessCheckEq(deferCount, 3ULL, GlobalRcu().log());
    }
}


/******************************************************************************/
/* RCU THREAD                                                                 */
/******************************************************************************/

/** Since GlobalRcu's implementation heavily relies on TLS this util creates a
    new TLS node and performs op that node.

    Note that all the ops are fully synchronized and will not return until the
    thread has completed the op. So while we're operating on multiple threads,
    there's no parallelism taking place here which is why this isn't in
    grcu_para_test.cpp.
 */
struct RcuThread
{
    RcuThread() : action(0), th([=] { this->run(); }) {}
    ~RcuThread() { die(); }

    void run()
    {
        GlobalRcu rcu;

        unique_lock<mutex> guard(lock);
        while (true) {
            while (!action) cvar.wait(guard);

            bool exit = false;

            if (action == 1) exit = true;
            if (action == 2) epochs.push(rcu.enter());
            if (action == 3) {
                rcu.exit(epochs.top());
                epochs.pop();
            }
            if (action == 4) rcu.defer(deferFn);

            action = 0;
            cvar.notify_one();

            if (exit) return;
        }
    }

    void doEvent(int ev)
    {
        unique_lock<mutex> guard(lock);
        action = ev;
        cvar.notify_one();
        while (action) cvar.wait(guard);
    }

    size_t enter()
    {
        doEvent(2);
        return epochs.top();
    }
    size_t exit()
    {
        size_t epoch = epochs.top();
        doEvent(3);
        return epoch;
    }
    void defer(const function<void()>& fn)
    {
        deferFn = fn;
        doEvent(4);
    }
    void die()
    {
        doEvent(1);
        th.join();
    }

    size_t active() const { return epochs.size(); }

    atomic<int> action;

    mutex lock;
    condition_variable cvar;

    function<void()> deferFn;
    stack<size_t> epochs;

    thread th;
};

void enterExit()
{
    thread([] { RcuGuard<GlobalRcu>(GlobalRcu()); }).join();
}

// Quick test to make sure that our RcuThread utility works as expected.
BOOST_AUTO_TEST_CASE(threadedSmokeTest)
{
    cerr << fmtTitle("threadedSmokeTest", '=') << endl;
    size_t deferCount = 0;

    {
        RcuThread th;
        size_t e0 = th.enter();
        th.defer([&] { deferCount++; });
        th.defer([&] { deferCount += 10; });
        size_t e1 = th.exit();

        locklessCheckEq(e0, e1, NullLog);
    }

    locklessCheckEq(deferCount, 11ULL, GlobalRcu().log());
}

/** Since GlobalRcu relies heavily on TLS, we need to make sure that the RCU
    guarantees still hold when access from various threads. Note that all these
    threads execute in lock step and are fully synchronized. There's 0
    parallelism here.
 */
BOOST_AUTO_TEST_CASE(threadedBasicsTest)
{
    cerr << fmtTitle("threadedBasicsTest", '=') << endl;

    GlobalRcu rcu;
    auto log = rcu.log();

    size_t deferCount;
    auto deferFn = [&] { deferCount++; };

    auto getEpoch = [&] {
        size_t epoch = rcu.enter();
        rcu.exit(epoch);
        return epoch;
    };

    for (int i = 0; i < 3; ++i) {
        RcuThread th0;
        RcuThread th1;
        deferCount = 0;

        cerr << fmtTitle("a, 0-0*, 0-0") << endl;
        cerr << rcu.print() << endl;
        size_t e0 = getEpoch();
        th0.enter();
        th1.defer(deferFn);

        cerr << fmtTitle("b, 1-1*, 0-0") << endl;
        cerr << rcu.print() << endl;
        {
            RcuThread thTemp;
            thTemp.defer(deferFn);
        }
        rcu.gc();
        locklessCheckEq(deferCount, 0ULL, log);

        cerr << fmtTitle("c, 1-2, 0-0*") << endl;
        cerr << rcu.print() << endl;
        th0.defer(deferFn);
        th1.enter();
        th1.defer(deferFn);
        size_t e1 = getEpoch();
        locklessCheckNe(e0, e1, log);
        locklessCheckEq(deferCount, 0ULL, log);

        cerr << fmtTitle("d, 1-2, 1-2*") << endl;
        cerr << rcu.print() << endl;
        RcuThread th2;
        th2.enter();
        rcu.gc();
        locklessCheckEq(e1, getEpoch(), log);
        locklessCheckEq(deferCount, 0ULL, log);

        cerr << fmtTitle("e, 1-2, 2-2*") << endl;
        cerr << rcu.print() << endl;
        th0.exit();
        rcu.gc();
        size_t e2 = getEpoch();
        locklessCheckNe(e2, e0, log);
        locklessCheckNe(e2, e1, log);
        locklessCheckEq(deferCount, 2ULL, log);

        cerr << fmtTitle("f, 0-0*, 2-2") << endl;
        cerr << rcu.print() << endl;
        th1.exit();
        rcu.gc();
        locklessCheckEq(getEpoch(), e2, log);
        locklessCheckEq(deferCount, 2ULL, log);

        cerr << fmtTitle("g, 0-0*, 1-2") << endl;
        cerr << rcu.print() << endl;
        th2.exit();
        rcu.gc();
        size_t e3 = getEpoch();
        locklessCheckNe(e3, e0, log);
        locklessCheckNe(e3, e1, log);
        locklessCheckNe(e3, e2, log);
        locklessCheckEq(deferCount, 4ULL, log);

        cerr << fmtTitle("h, 0-0, 0-0*") << endl;
        cerr << rcu.print() << endl;
        rcu.gc();
        size_t e4 = getEpoch();
        locklessCheckNe(e4, e0, log);
        locklessCheckNe(e4, e1, log);
        locklessCheckNe(e4, e2, log);
        locklessCheckNe(e4, e3, log);
        locklessCheckEq(deferCount, 4ULL, log);

        cerr << fmtTitle("i, 0-0*, 0-0") << endl;
        cerr << rcu.print() << endl;
    }
}

// Randomly do stuff hoping to discover some weird edge case that I couldn't
// think of.
BOOST_AUTO_TEST_CASE(fuzzTest)
{
    cerr << fmtTitle("fuzzTest", '=') << endl;

    enum { Rounds = 100000 };

    map<size_t, size_t> expected;
    map<size_t, size_t> counters;

    {
        mt19937_64 engine;
        uniform_int_distribution<unsigned> actionRnd(0, 6);

        vector<RcuThread*> actors;
        array<size_t, 2> inEpoch {{ 0, 0 }};

        auto getEpoch = [&] {
            GlobalRcu rcu;
            size_t epoch = rcu.enter();
            rcu.exit(epoch);
            return epoch;
        };


        for (size_t round = 0; round < Rounds; ++round) {
            if (round % 1000 == 0)
                cerr << "\r" << fmtValue(round) << " of " << fmtValue(Rounds);

            unsigned action = actionRnd(engine);

            if (actors.empty() || action == 0) {
                actors.emplace_back(new RcuThread());
                continue;
            }

            uniform_int_distribution<unsigned> actorRnd(0, actors.size() - 1);
            unsigned actor = actorRnd(engine);

            if (actors.size() > 5) {
                while (actors[actor]->active())
                    inEpoch[actors[actor]->exit() & 1]--;

                auto pred = [] (RcuThread* actor) {
                    if (actor->active()) return false;
                    delete actor;
                    return true;
                };

                actors.erase(
                        remove_if(actors.begin(), actors.end(), pred),
                        actors.end());
                continue;
            }

            else if (action == 2)
                inEpoch[actors[actor]->enter() & 1]++;

            else if (action == 3) {
                for (size_t i = 0; i < actors.size(); ++i) {
                    int j = (actor + i) % actors.size();
                    if (!actors[j]->active()) continue;

                    inEpoch[actors[j]->exit() & 1]--;
                    break;
                }
            }

            else if (action == 4 && GlobalRcu().gc())
                /* no-op */;

            else {
                size_t epoch = getEpoch();
                actors[actor]->defer([&, epoch] {
                            locklessCheckEq(inEpoch[epoch & 1], 0ULL, NullLog);
                            counters[epoch]++;
                        });
                expected[epoch]++;
            }
        }

        cerr << "\nLastEpoch: " << getEpoch() << endl;

        for (RcuThread* actor : actors) {
            while (actor->active()) actor->exit();
            delete actor;
        }
    }

    for (const auto& exp: expected)
        locklessCheckEq(counters[exp.first], exp.second, GlobalRcu().log());
}
