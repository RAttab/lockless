/* lock.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 12 Feb 2013
   FreeBSD-style copyright and disclaimer apply

   REALLY simple spin lock useful for debugging code.

   I'm also aware of how ironic this is...

   \todo Make this an actual robust spin lock.
*/

#ifndef __lockless__lock_h__
#define __lockless__lock_h__

#include <atomic>

namespace lockless {


/******************************************************************************/
/* LOCK                                                                       */
/******************************************************************************/

struct Lock
{
    Lock() : val(0) {}

    void lock()
    {
        size_t oldVal = 0;
        while(!val.compare_exchange_weak(oldVal, 1)) oldVal = 0;
    }

    void unlock() { val.store(0); }

private:
    std::atomic<size_t> val;
};



} // lockless

#endif // __lockless__lock_h__
