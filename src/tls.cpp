/* tls.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 01 Sep 2013
   FreeBSD-style copyright and disclaimer apply

   Thread id implementation.
*/

#include "tls.h"

#include <atomic>

using namespace std;

namespace lockless {


/******************************************************************************/
/* THREAD ID                                                                  */
/******************************************************************************/

namespace {

atomic<size_t> GlobalThreadCounter{1};
locklessTls size_t LocalThreadId{0};

} // namespace anonymous


size_t threadId()
{
    if (!LocalThreadId)
        LocalThreadId = GlobalThreadCounter.fetch_add(1);
    return LocalThreadId;
}


} // lockless
