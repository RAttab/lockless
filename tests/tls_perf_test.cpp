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
void doTlsThread(unsigned, unsigned itCount)
{
    *tls = 0;
    size_t value;

    for (size_t i = 0; i < itCount; ++i) {
        value = *tls;
        *tls = value + 1;
    }
}


/******************************************************************************/
/* GCC OPS                                                                    */
/******************************************************************************/

// Volatile forces a read and a write on each iteration.
locklessTls volatile size_t gccTls;

void doGccThread(unsigned, unsigned itCount)
{
    gccTls = 0;
    size_t value;

    for (size_t i = 0; i < itCount; ++i) {
        value = gccTls;
        gccTls = value + 1;
    }
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
    time. But if won't don't do this then the entire loop can be optimized out
    which is no better.

    Oh well, mostly disregard the results of this test.
*/
void doPthreadThread(PthreadContext&, unsigned itCount)
{
    pthread_setspecific(pthreadKey, new size_t());

    size_t* value;

    for (size_t i = 0; i < itCount; ++i) {
        value = static_cast<size_t*>(pthread_getspecific(pthreadKey));\
        *value += 1;
        pthread_setspecific(pthreadKey, value);
    }
}


/******************************************************************************/
/* MAIN                                                                       */
/******************************************************************************/

int main(int argc, char** argv)
{
    unsigned thCount = 2;
    if (argc > 1) thCount = stoul(string(argv[1]));

    size_t itCount = 1000000;
    if (argc > 2) itCount = stoull(string(argv[2]));

    bool csvOutput = false;
    if (argc > 3) csvOutput = stoi(string(argv[3]));

    Format fmt = csvOutput ? Csv : Human;

    PerfTest<unsigned> tlsPerf;
    tlsPerf.add(doTlsThread, thCount, itCount);
    tlsPerf.run();
    cerr << dump(tlsPerf, 0, "tls", fmt) << endl;

    PerfTest<unsigned> gccPerf;
    gccPerf.add(doGccThread, thCount, itCount);
    gccPerf.run();
    cerr << dump(gccPerf, 0, "gcc", fmt) << endl;

    PerfTest<PthreadContext> pthreadPerf;
    pthreadPerf.add(doPthreadThread, thCount, itCount);
    pthreadPerf.run();
    cerr << dump(pthreadPerf, 0, "pthread", fmt) << endl;

    return 0;

}

