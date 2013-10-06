/* snzi_perf_test.cc                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 06 Oct 2013
   FreeBSD-style copyright and disclaimer apply

   Performance tests for snzi.

   Turns out that replacing a single lightweight atomic inc instructions by
   metric ton of cas-spins is a bad idea... It's a shocker really.

   So the problem is that either a thread is alone on its node and therefor
   always has to climb up the tree or the thread isn't alone on its node and has
   to loop multiple time on the cas to get anything done. Either way you're
   replacing a single contented atomic inc by multiple contended cas-loops which
   is horrible.

   I'm pretty sure this thing would work better if the underlying arch didn't
   have an atomic inc which means that we'd be trading a heavily contended
   cas-loops for multiple lightly contended cas-loops. Not the case for x86 so
   this is pretty much useless.

*/

#include "snzi.h"
#include "perf_utils.h"

using namespace std;
using namespace lockless;

enum { Iterations = 100 };


/******************************************************************************/
/* OPS                                                                        */
/******************************************************************************/

typedef std::atomic<size_t> CasContext;

size_t doCasThread(CasContext& value, unsigned)
{
    for (size_t it = 0; it < Iterations; ++it) {
        size_t old = value;
        while (!value.compare_exchange_weak(old, old + 1));

        old = value;
        while (!value.compare_exchange_weak(old, old - 1));
    }

    return Iterations;
}

template<typename Snzi>
size_t doIncDecThread(Snzi& snzi, unsigned)
{
    for (size_t it = 0; it < Iterations; ++it) {
        snzi.inc();
        snzi.dec();
    }

    return Iterations;
}

template<typename Snzi>
size_t doTestThread(Snzi& snzi, unsigned)
{
    for (size_t it = 0; it < Iterations; ++it)
        snzi.test();

    return Iterations;
}


/******************************************************************************/
/* RUNNERS                                                                    */
/******************************************************************************/

template<size_t Nodes, size_t Arity>
void runTest(const string& name, unsigned thCount, size_t lengthMs)
{
    typedef Snzi<Nodes, Arity> Snzi;

    PerfTest<Snzi> perf;
    perf.add("inc", doIncDecThread<Snzi>, thCount);
    perf.add("test", doTestThread<Snzi>, 1);
    perf.run(lengthMs);

    cerr << perf.stats("inc").print("inc-" + name) << endl
        << perf.stats("test").print("test-" + name) << endl
        << endl;
}


/******************************************************************************/
/* MAIN                                                                       */
/******************************************************************************/

int main(int argc, char** argv)
{
    unsigned thCount = 7;
    if (argc > 1) thCount = stoul(string(argv[1]));

    size_t lengthMs = 1000;
    if (argc > 2) lengthMs = stoull(string(argv[2]));

    {
        PerfTest<CasContext> perf;
        perf.add("cas", doCasThread, thCount);
        perf.run(lengthMs);
        cerr << perf.printStats("cas") << endl;
    }

    runTest<1, 1>("snzi-1-1", thCount, lengthMs);
    runTest<4, 2>("snzi-4-2", thCount, lengthMs);
    runTest<8, 2>("snzi-8-2", thCount, lengthMs);
    runTest<8, 4>("snzi-8-4", thCount, lengthMs);

    return 0;
}
