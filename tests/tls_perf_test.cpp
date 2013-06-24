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
Tls<size_t, Tag> tls;

void doTlsThread(unsigned, unsigned itCount)
{
    tls = 0;
    volatile size_t value; // Forces a read and a write on each iteration

    for (size_t i = 0; i < itCount; ++i) {
        value = tls;
        tls = value + 1;
    }
}


/******************************************************************************/
/* GCC OPS                                                                    */
/******************************************************************************/
// \todo Need an attribute to disable optimizations on this one.

locklessTls size_t gccTls;

void doGccThread(unsigned, unsigned itCount)
{
    gccTls = 0;
    volatile size_t value; // Forces a read and a write on each iteration

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

