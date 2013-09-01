/* queue_perf_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 17 Mar 2013
   FreeBSD-style copyright and disclaimer apply

   Performance for the unbounded lockfree queue.
*/

#include "queue.h"
#include "perf_utils.h"

using namespace std;
using namespace lockless;

/******************************************************************************/
/* OPS                                                                        */
/******************************************************************************/

struct Context
{
    Queue<size_t> queue;
};

enum { Iterations = 100 };

size_t doPushThread(Context& ctx, unsigned)
{
    for (size_t i = 0; i < Iterations; ++i)
        ctx.queue.push(1);
    return Iterations;
}

size_t doPopThread(Context& ctx, unsigned)
{
    for (size_t i = 0; i < Iterations; ++i)
        ctx.queue.pop();
    return Iterations;
}

/******************************************************************************/
/* MAIN                                                                       */
/******************************************************************************/

int main(int argc, char** argv)
{
    unsigned thCount = 4;
    if (argc > 1) thCount = stoul(string(argv[1]));

    size_t lengthMs = 1000;
    if (argc > 2) lengthMs = stoull(string(argv[2]));

    PerfTest<Context> perf;
    perf.add("push", doPushThread, thCount);
    perf.add("pop", doPopThread, thCount);

    perf.run(lengthMs);

    cerr << perf.printStats("push") << endl;
    cerr << perf.printStats("pop") << endl;

    return 0;

}
