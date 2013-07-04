/* alloc_para_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 03 Jul 2013
   FreeBSD-style copyright and disclaimer apply

   Parallel tests for the block allocator.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#define LOCKLESS_ALLOC_DEBUG 1
#define LOCKLESS_CHECK_ABORT 1


#include "alloc.h"
#include "check.h"
#include "debug.h"
#include "test_utils.h"
#include "alloc_test.h"

#include <boost/test/unit_test.hpp>
#include <random>


using namespace std;
using namespace lockless;


/******************************************************************************/
/* RING BUFFER TEST                                                           */
/******************************************************************************/

template<typename Value, size_t Size>
void typedRingBufferTest()
{
    enum {
        Threads = 8,
        Iterations = 1000,
    };

    string title = format("value=%ld, size=%ld", Value().v.size(), Size);
    cerr << fmtTitle(title) << endl;

    std::array<std::atomic<Value*>, Size> values;
    for (auto& value : values) value = nullptr;

    auto doThread = [&] (unsigned id) {
        size_t start = (values.size() / Threads) * id;

        for (size_t it = 0; it < Iterations; ++it) {
            for (size_t i = 0; i < values.size(); ++i) {
                size_t index = (start + i) % values.size();
                Value* old = values[index].exchange(new Value());
                if (old) delete old;
            }
        }
    };

    ParallelTest test;
    test.add(doThread, Threads);
    test.run();
}

BOOST_AUTO_TEST_CASE(ringBufferTest)
{
    cerr << fmtTitle("Ring Buffer", '=') << endl;

    typedRingBufferTest<SmallValue, 10>();
    typedRingBufferTest<SmallValue, 1000>();

    typedRingBufferTest<BigValue, 10>();
    typedRingBufferTest<BigValue, 1000>();

    typedRingBufferTest<HugeValue, 10>();
    typedRingBufferTest<HugeValue, 1000>();
}
