/* alloc_perf_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 03 Jul 2013
   FreeBSD-style copyright and disclaimer apply

   Performance tests to compare tcmalloc and balloc.
*/

#include "alloc.h"
#include "arena.h"
#include "perf_utils.h"

#include <array>
#include <atomic>

using namespace std;
using namespace lockless;


/******************************************************************************/
/* VALUES                                                                     */
/******************************************************************************/

template<size_t Size, typename Tag>
struct Value;


/******************************************************************************/
/* TCMALLOC VALUE                                                             */
/******************************************************************************/

struct TCMallocTag {};

template<size_t Size>
struct Value<Size, TCMallocTag>
{
    static const char* name;
    array<uint64_t, Size> v;
};

template<size_t Size>
const char* Value<Size, TCMallocTag>::name = "tcmalloc";


/******************************************************************************/
/* BALLOC VALUE                                                               */
/******************************************************************************/

struct BallocTag {};

template<size_t Size>
struct Value<Size, BallocTag>
{
    static const char* name;
    array<uint64_t, Size> v;

    typedef Value<Size, BallocTag> AllocType;
    LOCKLESS_BLOCK_ALLOC_TYPED_OPS(AllocType)
};

template<size_t Size>
const char* Value<Size, BallocTag>::name = "balloc";


/******************************************************************************/
/* ARENA VALUE                                                                */
/******************************************************************************/

struct ArenaTag {};

template<size_t Size>
struct Value<Size, ArenaTag>
{
    static const char* name;
    array<uint64_t, Size> v;

    static Arena<PageSize> arena;

    typedef Value<Size, ArenaTag> AllocType;
    void* operator new(size_t size)
    {
        return arena.alloc(size);
    }
    void operator delete(void*) {}
};

template<size_t Size>
const char* Value<Size, ArenaTag>::name = "arena";

template<size_t Size>
Arena<PageSize> Value<Size, ArenaTag>::arena;


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
        size_t index = it % ctx.ring.size();
        Value* old = ctx.ring[index].exchange(new Value());
        delete old;
    }
}


/******************************************************************************/
/* MAIN                                                                       */
/******************************************************************************/

template<typename Tag, size_t ValueSize, size_t RingSize>
void runRingBufferTest(unsigned thCount, unsigned itCount, Format fmt)
{
    typedef Value<ValueSize, Tag> Value;
    typedef RingBuffer<Value, RingSize> Context;

    PerfTest<Context> perf;
    perf.add(doRingBufferThread<Value, RingSize>, thCount, itCount);
    perf.run();

    cerr << dump(perf, 0, Value::name, fmt) << endl;
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

    bool arena = false; // segfaults :(
    bool balloc = true;
    bool tcmalloc = true;

    cerr << format("value=%s, ring=%s\n",
            fmtValue(1).c_str(), fmtValue(10).c_str());
    if (arena) runRingBufferTest<ArenaTag, 1, 10>(thCount, itCount, fmt);
    if (balloc) runRingBufferTest<BallocTag, 1, 10>(thCount, itCount, fmt);
    if (tcmalloc) runRingBufferTest<TCMallocTag, 1, 10>(thCount, itCount, fmt);

    cerr << format("\nvalue=%s, ring=%s\n",
            fmtValue(1).c_str(), fmtValue(1000).c_str());
    if (arena) runRingBufferTest<ArenaTag, 1, 1000>(thCount, itCount, fmt);
    if (balloc) runRingBufferTest<BallocTag, 1, 1000>(thCount, itCount, fmt);
    if (tcmalloc) runRingBufferTest<TCMallocTag, 1, 1000>(thCount, itCount, fmt);

    cerr << format("\nvalue=%s, ring=%s\n",
            fmtValue(11).c_str(), fmtValue(10).c_str());
    if (arena) runRingBufferTest<ArenaTag, 11, 10>(thCount, itCount, fmt);
    if (balloc) runRingBufferTest<BallocTag, 11, 10>(thCount, itCount, fmt);
    if (tcmalloc) runRingBufferTest<TCMallocTag, 11, 10>(thCount, itCount, fmt);

    cerr << format("\nvalue=%s, ring=%s\n",
            fmtValue(11).c_str(), fmtValue(1000).c_str());
    if (arena) runRingBufferTest<ArenaTag, 11, 1000>(thCount, itCount, fmt);
    if (balloc) runRingBufferTest<BallocTag, 11, 1000>(thCount, itCount, fmt);
    if (tcmalloc) runRingBufferTest<TCMallocTag, 11, 1000>(thCount, itCount, fmt);

    cerr << format("\nvalue=%s, ring=%s\n",
            fmtValue(65).c_str(), fmtValue(10).c_str());
    if (arena) runRingBufferTest<ArenaTag, 65, 10>(thCount, itCount, fmt);
    if (balloc) runRingBufferTest<BallocTag, 65, 10>(thCount, itCount, fmt);
    if (tcmalloc) runRingBufferTest<TCMallocTag, 65, 10>(thCount, itCount, fmt);

    cerr << format("\nvalue=%s, ring=%s\n",
            fmtValue(65).c_str(), fmtValue(1000).c_str());
    if (arena) runRingBufferTest<ArenaTag, 65, 1000>(thCount, itCount, fmt);
    if (balloc) runRingBufferTest<BallocTag, 65, 1000>(thCount, itCount, fmt);
    if (tcmalloc) runRingBufferTest<TCMallocTag, 65, 1000>(thCount, itCount, fmt);

    return 0;

}

