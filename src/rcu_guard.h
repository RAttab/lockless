/* rcu_guard.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 11 May 2013
   FreeBSD-style copyright and disclaimer apply

   RAII for Rcu objects.
*/

#ifndef __lockless__rcu_guard_h__
#define __lockless__rcu_guard_h__

#include <cstddef>

namespace lockless {

/******************************************************************************/
/* RCU GUARD                                                                  */
/******************************************************************************/

template<typename Rcu>
struct RcuGuard
{
    RcuGuard(Rcu& rcu) :
        rcu(rcu),
        epoch(rcu.enter())
    {}

    ~RcuGuard() { rcu.exit(epoch); }

private:

    Rcu& rcu;
    size_t epoch;

};


} // lockless

#endif // __lockless__rcu_guard_h__
