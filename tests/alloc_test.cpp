/* alloc_test.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 18 Jun 2013
   FreeBSD-style copyright and disclaimer apply

   tests for the block allocator.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "alloc.h"
#include "check.h"
#include "debug.h"
#include "test_utils.h"

#include <boost/test/unit_test.hpp>


using namespace std;
using namespace lockless;


void checkPow2(size_t val)
{
    locklessCheckEq(val & (val - 1), 0ULL, NullLog);
}

void checkAlign(size_t val, size_t align)
{
    checkPow2(align);
    locklessCheckEq(val & (align - 1), 0ULL, NullLog);
}

template<typename Policy>
void checkPolicy(const std::function<void (size_t, size_t)>& fn)
{
    // cerr << fmtTitle(
    //         "blockSize=" + to_string(Policy::BlockSize)
    //         + ", pageSize=" + to_string(Policy::PageSize))
    //     << endl;

    fn(Policy::BlockSize, Policy::PageSize);
}

#define PolicyTest(Policy, checkFn)                     \
    do {                                                \
        checkPolicy< Policy<0x00> >(checkFn);           \
        checkPolicy< Policy<0x01> >(checkFn);           \
        checkPolicy< Policy<0x02> >(checkFn);           \
        checkPolicy< Policy<0x03> >(checkFn);           \
        checkPolicy< Policy<0x04> >(checkFn);           \
        checkPolicy< Policy<0x05> >(checkFn);           \
        checkPolicy< Policy<0x06> >(checkFn);           \
        checkPolicy< Policy<0x07> >(checkFn);           \
        checkPolicy< Policy<0x08> >(checkFn);           \
        checkPolicy< Policy<0x0C> >(checkFn);           \
        checkPolicy< Policy<0x10> >(checkFn);           \
        checkPolicy< Policy<0x18> >(checkFn);           \
        checkPolicy< Policy<0x20> >(checkFn);           \
        checkPolicy< Policy<0x30> >(checkFn);           \
        checkPolicy< Policy<0x40> >(checkFn);           \
        checkPolicy< Policy<0x80> >(checkFn);           \
        checkPolicy< Policy<0x0100> >(checkFn);         \
        checkPolicy< Policy<0x0200> >(checkFn);         \
        checkPolicy< Policy<0x0400> >(checkFn);         \
        checkPolicy< Policy<0x0800> >(checkFn);         \
        checkPolicy< Policy<0x1000> >(checkFn);         \
        checkPolicy< Policy<0x0000010000> >(checkFn);   \
        checkPolicy< Policy<0x0000100000> >(checkFn);   \
        checkPolicy< Policy<0x0001000000> >(checkFn);   \
    } while(false);


BOOST_AUTO_TEST_CASE(policyTest)
{
    auto checkFn = [] (size_t blockSize, size_t pageSize) {
        checkPow2(pageSize);
        locklessCheckGe(pageSize, 4096ULL, NullLog);

        if (blockSize)
            locklessCheckGt(pageSize / blockSize, 64ULL, NullLog);
    };

    auto checkAlignFn = [&] (size_t blockSize, size_t pageSize) {
        checkFn(blockSize, pageSize);
        checkAlign(blockSize, 8);
    };

    cerr << fmtTitle("PackedAllocPolicy", '=') << endl;
    PolicyTest(PackedAllocPolicy, checkFn);

    cerr << fmtTitle("AlignedAllocPolicy", '=') << endl;
    PolicyTest(AlignedAllocPolicy, checkAlignFn);
}

BOOST_AUTO_TEST_CASE(pageTest)
{

}
