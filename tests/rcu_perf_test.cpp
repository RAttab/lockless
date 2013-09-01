/* rcu_perf_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 31 Dec 2012
   FreeBSD-style copyright and disclaimer apply

   Performance tests for RCU.
*/

#include "rcu.h"
#include "grcu.h"
#include "perf_utils.h"

#include <atomic>

using namespace std;
using namespace lockless;

enum { Iterations = 100 };

/******************************************************************************/
/* OPS                                                                        */
/******************************************************************************/

template<typename Rcu>
struct Context
{
    Rcu rcu;
    atomic<size_t> counter;
};

template<typename Rcu>
size_t doEnterExitThread(Context<Rcu>& ctx, unsigned)
{
    for (size_t it = 0; it < Iterations; ++it)
        RcuGuard<Rcu> guard(ctx.rcu);

    return Iterations;
}

template<typename Rcu>
size_t doDeferThread(Context<Rcu>& ctx, unsigned)
{
    for (size_t it = 0; it < Iterations; ++it)
        ctx.rcu.defer([&] { ctx.counter++; });

    return Iterations;
}


/******************************************************************************/
/* MAIN                                                                       */
/******************************************************************************/

template<typename Rcu>
void runEnterExitTest(unsigned thCount, size_t lengthMs, const string& name)
{
    typedef Context<Rcu> Ctx;

    PerfTest<Ctx> perf;
    perf.add("epochs", doEnterExitThread<Rcu>, thCount);
    perf.run(lengthMs);

    cerr << perf.stats("epochs").print("epochs-" + name) << endl;
}


template<typename Rcu>
void runDeferTest(unsigned thCount, size_t lengthMs, const string& name)
{
    typedef Context<Rcu> Ctx;

    PerfTest<Ctx> perf;
    perf.add("epochs", doEnterExitThread<Rcu>, thCount);
    perf.add("defer", doDeferThread<Rcu>, thCount);
    perf.run(lengthMs);

    cerr << perf.stats("defer").print("defer-" + name) << endl;
}

int main(int argc, char** argv)
{
    unsigned thCount = 4;
    if (argc > 1) thCount = stoul(string(argv[1]));

    size_t lengthMs = 1000;
    if (argc > 2) lengthMs = stoull(string(argv[2]));

    runEnterExitTest<Rcu>(thCount, lengthMs, "rcu");
    {
        GcThread gcThread;
        runEnterExitTest<GlobalRcu>(thCount, lengthMs, "grcu");
    }

    runDeferTest<Rcu>(thCount, lengthMs, "rcu");
    {
        GcThread gcThread;
        runDeferTest<GlobalRcu>(thCount, lengthMs, "grcu");
    }

    return 0;
}
