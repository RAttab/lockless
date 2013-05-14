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


BOOST_AUTO_TEST_CASE(smokeTest)
{
    cerr << fmtTitle("smokeTest", '=') << endl;

    GlobalRcu rcu;
    RcuGuard<GlobalRcu> guard(rcu);
    cerr << rcu.print() << endl;
}

BOOST_AUTO_TEST_CASE(epochTest)
{
    cerr << fmtTitle("epochTest", '=') << endl;

    GlobalRcu rcu;
    auto log = rcu.log();

    for (size_t i = 0; i < 5; ++i) {
        cerr << fmtTitle("a, 0, 0") << endl;
        cerr << rcu.print() << endl;


        size_t e0 = rcu.enter();
        locklessCheckEq(rcu.enter(), e0, log);
        cerr << fmtTitle("b, 2, 0") << endl;
        cerr << rcu.print() << endl;

        rcu.exit(e0);
        rcu.gc();
        size_t e1 = rcu.enter();
        cerr << fmtTitle("c, 1, 1") << endl;
        cerr << rcu.print() << endl;
        locklessCheckNe(e1, e0, log);

        rcu.gc();
        locklessCheckEq(rcu.enter(), e1, log);
        cerr << fmtTitle("d, 1, 2") << endl;
        cerr << rcu.print() << endl;

        rcu.exit(e0);
        rcu.gc();
        size_t e2 = rcu.enter();
        cerr << fmtTitle("e, 1, 2") << endl;
        cerr << rcu.print() << endl;
        locklessCheckNe(e2, e0, log);
        locklessCheckNe(e2, e1, log);

        rcu.exit(e2);
        rcu.exit(e1);
        rcu.exit(e1);
        rcu.gc();
        cerr << fmtTitle("f, 0, 0") << endl;
        cerr << rcu.print() << endl;
    }
}


/******************************************************************************/
/* RCU THREAD                                                                 */
/******************************************************************************/

/** Since GlobalRcu's implementation heavily relies on TLS this util creates a
    new TLS node and performs op that node.
 */
struct RcuThread
{
    RcuThread() : th([=] { this->run(); }), action(0) {}

    void run()
    {
        unique_lock<mutex> guard(lock);
        while(true) {
            while (!action) cvar.wait(guard);

            if (action == 1) break;
            if (action == 2) epochs.push(rcu.enter());
            if (action == 3) {
                rcu.exit(epochs.top());
                epochs.pop();
            }
            if (action == 4) rcu.defer(deferFn);

            action = 0;
            cvar.notify_one();
        }

        action = 0;
        cvar.notify_one();
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

    thread th;
    GlobalRcu rcu;

    mutex lock;
    condition_variable cvar;
    atomic<int> action;
    stack<size_t> epochs;
    function<void()> deferFn;
};

BOOST_AUTO_TEST_CASE(threadedSmokeTest)
{
    cerr << fmtTitle("threadedSmokeTest", '=') << endl;

    GlobalRcu rcu;
    RcuThread th;
    th.enter();
    th.defer([] { cerr << "BOO!" << endl; });
    th.defer([] { cerr << "BLAH!" << endl; });
    th.exit();
    cerr << rcu.print() << endl;
    th.die();
}


BOOST_AUTO_TEST_CASE(threadedBasicsTest)
{
    cerr << fmtTitle("threadedBasicsTest", '=') << endl;

    for (int i = 0; i < 5; ++i) {

    }
}

