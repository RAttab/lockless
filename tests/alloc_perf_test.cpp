/* alloc_perf_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 03 Jul 2013
   FreeBSD-style copyright and disclaimer apply

   Performance tests to compare tcmalloc and balloc.
*/

#include "alloc.h"
#include "perf_utils.h"

#include <array>
#include <atomic>

using namespace std;
using namespace lockless;


/******************************************************************************/
/* VALUES                                                                     */
/******************************************************************************/

template<size_t Size, bool Balloc>
struct Value
{
    array<uint64_t, Size> v;
};


template<size_t Size>
struct Value<Size, true>
{
    array<uint64_t, Size> v;

    typedef Value<Size, true> AllocType;
    LOCKLESS_BLOCK_ALLOC_TYPED_OPS(AllocType)
};


/******************************************************************************/
/* CONTEXT                                                                    */
/******************************************************************************/

template<typename Value_, size_t Size>
struct RingBuffer
{
    typedef Value_ Value;
    array<atomic<Value*>, Size> ring;

    RingBuffer()
    {
        fill(ring.begin(), ring.end(), nullptr);
    }

    ~RingBuffer()
    {
        for (Value* value : ring) delete value;
    }
};


/******************************************************************************/
/* RING BUFFER                                                                */
/******************************************************************************/

template<typename Value, size_t Size>
void doRingBufferThread(RingBuffer<Value, Size>& ctx, unsigned itCount)
{
    for (size_t it = 0; it < itCount; ++it) {
        Value* old = ctx.ring[it % ctx.ring.size()].exchange(new Value());
        if (old) delete old;
    }
}


/******************************************************************************/
/* MAIN                                                                       */
/******************************************************************************/

template<bool Balloc, size_t ValueSize, size_t RingSize>
void runRingBufferTest(unsigned thCount, unsigned itCount, Format fmt)
{
    typedef Value<ValueSize, Balloc> Value;
    typedef RingBuffer<Value, RingSize> Context;

    PerfTest<Context> perf;
    perf.add(doRingBufferThread<Value, RingSize>, thCount, itCount);
    perf.run();

    string title = format("<%s, %s>:%s",
            string(Balloc ? "  balloc" : "tcmalloc").c_str(),
            fmtValue(ValueSize).c_str(),
            fmtValue(RingSize).c_str());
    cerr << dump(perf, 0, title, fmt) << endl;
}

int main(int argc, char** argv)
{
    unsigned thCount = 2;
    if (argc > 1) thCount = stoul(string(argv[1]));

    size_t itCount = 1000000;
    if (argc > 2) itCount = stoull(string(argv[2]));

    bool csvOutput = false;
    if (argc > 3) csvOutput = stoi(string(argv[3]));

    Format fmt = csvOutput ? Csv : Human;

    runRingBufferTest<true, 1, 10>(thCount, itCount, fmt);
    // runRingBufferTest<false, 1, 10>(thCount, itCount, fmt);
    // runRingBufferTest<true, 1, 1000>(thCount, itCount, fmt);
    // runRingBufferTest<false, 1, 1000>(thCount, itCount, fmt);

    // cerr << endl;

    // runRingBufferTest<true, 11, 10>(thCount, itCount, fmt);
    // runRingBufferTest<false, 11, 10>(thCount, itCount, fmt);
    // runRingBufferTest<true, 11, 1000>(thCount, itCount, fmt);
    // runRingBufferTest<false, 11, 1000>(thCount, itCount, fmt);

    // cerr << endl;

    // runRingBufferTest<true, 65, 10>(thCount, itCount, fmt);
    // runRingBufferTest<false, 65, 10>(thCount, itCount, fmt);
    // runRingBufferTest<true, 65, 1000>(thCount, itCount, fmt);
    // runRingBufferTest<false, 65, 1000>(thCount, itCount, fmt);

    return 0;

}

