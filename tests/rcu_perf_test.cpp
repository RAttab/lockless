/* rcu_perf_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 31 Dec 2012
   FreeBSD-style copyright and disclaimer apply

   Performance tests for RCU.
*/

#include "rcu.h"
#include "perf_utils.h"

#include <atomic>

using namespace std;
using namespace lockless;


/******************************************************************************/
/* OPS                                                                        */
/******************************************************************************/

struct Context
{
    Rcu rcu;
    atomic<size_t> counter;
};

void doEnterExitThread(Context& ctx, unsigned itCount)
{
    for (size_t it = 0; it < itCount; ++it)
        RcuGuard guard(ctx.rcu);
}

void doDeferThread(Context& ctx, unsigned itCount)
{
    for (size_t it = 0; it < itCount; ++it)
        ctx.rcu.defer([&] { ctx.counter++; });
}


/******************************************************************************/
/* MAIN                                                                       */
/******************************************************************************/

int main(int argc, char** argv)
{
    unsigned thCount = 1;
    if (argc > 1) thCount = stoul(string(argv[1]));

    size_t itCount = 100000;
    if (argc > 2) itCount = stoull(string(argv[2]));

    bool csvOutput = false;
    if (argc > 3) csvOutput = stoi(string(argv[3]));

    Format fmt = csvOutput ? Csv : Human;

    PerfTest<Context> perf;
    perf.add(doEnterExitThread, thCount, itCount);
    perf.add(doDeferThread, thCount, itCount);

    perf.run();

    array<string, 2> titles {{ "epochs", "defer" }};
    for (unsigned gr = 0; gr < 2; ++gr)
        cerr << dump(perf, gr, titles[gr], fmt) << endl;

    return 0;
}
