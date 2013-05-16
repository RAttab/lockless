/* grcu_test.cc                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 28 Apr 2013
   FreeBSD-style copyright and disclaimer apply

   Tests for the global rcu implementation.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#define LOCKLESS_GRCU_DEBUG 1

#include "grcu.h"
#include "check.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>
#include <iostream>
#include <stack>

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

    void enter() { doEvent(2); }
    void exit() { doEvent(3); }
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
        th.enter();
        th.defer([&] { deferCount++; });
        th.defer([&] { deferCount += 10; });
        th.exit();
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
