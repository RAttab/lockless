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
void doEnterExitThread(Context<Rcu>& ctx, unsigned itCount)
{
    for (size_t it = 0; it < itCount; ++it)
        RcuGuard<Rcu> guard(ctx.rcu);
}

template<typename Rcu>
void doDeferThread(Context<Rcu>& ctx, unsigned itCount)
{
    for (size_t it = 0; it < itCount; ++it)
        ctx.rcu.defer([&] { ctx.counter++; });
}


/******************************************************************************/
/* MAIN                                                                       */
/******************************************************************************/

template<typename Rcu>
void runTest(
        unsigned thCount,
        size_t itCount,
        Format fmt,
        const array<string, 2>& titles )
{
    PerfTest< Context<Rcu> > perf;
    perf.add(doEnterExitThread<Rcu>, thCount, itCount);
    perf.add(doDeferThread<Rcu>, thCount, itCount);

    perf.run();

    for (unsigned gr = 0; gr < 2; ++gr)
        cerr << dump(perf, gr, titles[gr], fmt) << endl;
}

int main(int argc, char** argv)
{
    unsigned thCount = 4;
    if (argc > 1) thCount = stoul(string(argv[1]));

    size_t itCount = 10000;
    if (argc > 2) itCount = stoull(string(argv[2]));

    bool csvOutput = false;
    if (argc > 3) csvOutput = stoi(string(argv[3]));

    Format fmt = csvOutput ? Csv : Human;

    runTest<Rcu>(thCount, itCount, fmt, {{ "rcu-epochs", "rcu-defer" }});
    {
        GcThread gcThread;
        runTest<GlobalRcu>(
                thCount, itCount, fmt, {{ "grcu-epochs", "grcu-defer" }});
    }


    return 0;
}
