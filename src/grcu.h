/* grcu.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 01 Apr 2013
   FreeBSD-style copyright and disclaimer apply

   Single instance read-copy-update implementation that make use of TLS to
   eliminate contention to enter and exit the read-side critical section. This
   implementation is far more scalable then the lightweight Rcu implementation
   in rcu.h but requires manual calls to gc() to execute defered work. This can
   either be delegated to a background thread or called in whatever manner best
   suits the user.

   Note that regardless of how many instances of GlobalRcu objects are created,
   they all share the same static and thread-local storage under the hood.
*/

#ifndef __lockless__grcu_h__
#define __lockless__grcu_h__

#include "rcu_guard.h"
#include "list.h"

#include <functional>

namespace lockless {


/******************************************************************************/
/* GLOBAL RCU                                                                 */
/******************************************************************************/

struct GlobalRcu
{
    typedef std::function<void()> DeferFn;

    GlobalRcu();
    ~GlobalRcu();

    size_t enter();
    void exit(size_t epoch);

    template<typename Fn>
    void defer(Fn&& fn)
    {
        defer(new ListNode<DeferFn>(std::forward<Fn>(fn)));
    }

    bool gc();


    std::string print() const;
    LogAggregator log();

private:
    void defer(ListNode<DeferFn>*);
};


/******************************************************************************/
/* GC THREAD                                                                  */
/******************************************************************************/

struct GcThread
{
    GcThread();
    ~GcThread() { join(); }

    void join();
    void detach();

    LogAggregator log();

private:
    bool joined;
};

} // lockless

#endif // __lockless__grcu_h__
