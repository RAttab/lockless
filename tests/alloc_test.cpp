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
#include <boost/preprocessor/seq/for_each.hpp>


using namespace std;
using namespace lockless;


#define SIZE_SEQ                                                       \
    (0x00) (0x01) (0x02) (0x03) (0x04) (0x05) (0x06) (0x07) (0x08) (0x0C) \
    (0x10) (0x11) (0x18) (0x20) (0x30) (0x40) (0x80)                    \
    (0x0100) (0x0111) (0x0200) (0x0400) (0x0800) (0x1000) (0x1111)      \
    (0x0000010000) (0x0000100000) (0x0001000000) (0x0001111111)


/******************************************************************************/
/* POLICY TEST                                                                */
/******************************************************************************/

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

BOOST_AUTO_TEST_CASE(policyTest)
{

    cerr << fmtTitle("PackedAllocPolicy", '=') << endl;

    auto checkFn = [] (size_t blockSize, size_t pageSize) {
        checkPow2(pageSize);
        locklessCheckGe(pageSize, 4096ULL, NullLog);

        if (blockSize)
            locklessCheckGt(pageSize / blockSize, 64ULL, NullLog);
    };

#define PackedAllocPolicyTest(_r_, _data_, _elem_)      \
    checkPolicy< PackedAllocPolicy<_elem_> >(checkFn);

    BOOST_PP_SEQ_FOR_EACH(PackedAllocPolicyTest, _, SIZE_SEQ);

#undef PackedAllocPolicyTest


    cerr << fmtTitle("AlignedAllocPolicy", '=') << endl;

    auto checkAlignFn = [&] (size_t blockSize, size_t pageSize) {
        checkFn(blockSize, pageSize);
        checkAlign(blockSize, 8);
    };


#define AlignedAllocPolicyTest(_r_, _data_, _elem_)             \
    checkPolicy< AlignedAllocPolicy<_elem_> >(checkAlignFn);

    BOOST_PP_SEQ_FOR_EACH(AlignedAllocPolicyTest, _, SIZE_SEQ);

#undef PackedAllocPolicyTest
}


/******************************************************************************/
/* PAGE TEST                                                                  */
/******************************************************************************/

BOOST_AUTO_TEST_CASE(pageTest)
{

}
