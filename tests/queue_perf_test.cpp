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

void doPushThread(Context& ctx, unsigned itCount)
{
    for (size_t i = 0; i < itCount; ++i)
        ctx.queue.push(1);
}

void doPopThread(Context& ctx, unsigned itCount)
{
    for (size_t i = 0; i < itCount; ++i)
        ctx.queue.pop();
}

/******************************************************************************/
/* MAIN                                                                       */
/******************************************************************************/

int main(int argc, char** argv)
{
    unsigned thCount = 4;
    if (argc > 1) thCount = stoul(string(argv[1]));

    size_t itCount = 10000;
    if (argc > 2) itCount = stoull(string(argv[2]));

    bool csvOutput = false;
    if (argc > 3) csvOutput = stoi(string(argv[3]));

    Format fmt = csvOutput ? Csv : Human;

    PerfTest<Context> perf;
    perf.add(doPushThread, thCount, itCount);
    perf.add(doPopThread, thCount, itCount);

    perf.run();

    array<string, 2> titles {{ "push", "pop" }};
    for (unsigned gr = 0; gr < 2; ++gr)
        cerr << dump(perf, gr, titles[gr], fmt) << endl;

    return 0;

}
