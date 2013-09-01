/* tls_perf_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 20 Apr 2013
   FreeBSD-style copyright and disclaimer apply

   Performance tests to compare pthread's tls versus elf/gcc tls.
*/

#include "tls.h"
#include "perf_utils.h"

#include <iostream>

using namespace std;
using namespace lockless;

enum { Iterations = 10000 };

/******************************************************************************/
/* TLS OPS                                                                    */
/******************************************************************************/

struct Tag;

// Volatile modifier forces a read and a write on each iteration
Tls<volatile size_t, Tag> tls;

/** The main performance difference between this and raw gcc tls is the extra
    indirection hit we get from having all TLS values be heap-allocated. Note
    that this comes down to a couple of nano seconds which means that it's still
    very competitive.

    So the conclussion is that our Tls class should only be used when our value
    isn't a POD or if we need the constructor and destructor callback.
*/
size_t doTlsThread(unsigned, unsigned)
{
    size_t value;

    for (size_t i = 0; i < Iterations; ++i) {
        value = *tls;
        *tls = value + 1;
    }

    return Iterations;
}


/******************************************************************************/
/* GCC OPS                                                                    */
/******************************************************************************/

// Volatile forces a read and a write on each iteration.
locklessTls volatile size_t gccTls;

size_t doGccThread(unsigned, unsigned)
{
    size_t value;

    for (size_t i = 0; i < Iterations; ++i) {
        value = gccTls;
        gccTls = value + 1;
    }

    return Iterations;
}


/******************************************************************************/
/* PTHREAD OPS                                                                */
/******************************************************************************/

static pthread_key_t pthreadKey;
struct PthreadContext
{
    PthreadContext()
    {
        pthread_key_create(&pthreadKey, nullptr);
    }

    ~PthreadContext()
    {
        pthread_key_delete(pthreadKey);
    }
};

/** Difficult to make a one-to-one mapping of pthread tls to gcc tls because any
    real use case would not write back to value using setSpecific every single
    time. But then we don't really take into account the cost of setspecific
    which is not really fair.

    Oh well, mostly disregard the results of this test.
*/
size_t doPthreadThread(PthreadContext&, unsigned)
{
    for (size_t i = 0; i < Iterations; ++i) {
        size_t* value = static_cast<size_t*>(pthread_getspecific(pthreadKey));
        if (!value) pthread_setspecific(pthreadKey, value = new size_t());

        *value += 1;

        pthread_setspecific(pthreadKey, value);
    }

    return Iterations;
}


/******************************************************************************/
/* MAIN                                                                       */
/******************************************************************************/

int main(int argc, char** argv)
{
    unsigned thCount = 8;
    if (argc > 1) thCount = stoul(string(argv[1]));

    size_t lengthMs = 1000;
    if (argc > 2) lengthMs = stoull(string(argv[2]));

    PerfTest<unsigned> tlsPerf;
    tlsPerf.add("tls", doTlsThread, thCount);
    tlsPerf.run(lengthMs);
    cerr << tlsPerf.printStats("tls") << endl;

    PerfTest<unsigned> gccPerf;
    gccPerf.add("gcc", doGccThread, thCount);
    gccPerf.run(lengthMs);
    cerr << gccPerf.printStats("gcc") << endl;

    PerfTest<PthreadContext> pthreadPerf;
    pthreadPerf.add("pthread", doPthreadThread, thCount);
    pthreadPerf.run(lengthMs);
    cerr << pthreadPerf.printStats("pthread") << endl;

    return 0;

}

