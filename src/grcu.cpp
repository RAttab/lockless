/* grcu.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 01 Apr 2013
   FreeBSD-style copyright and disclaimer apply

   Static Read-Copy-Update gRcuementation.
*/

#include "grcu.h"
#include "log.h"
#include "check.h"

#include <array>
#include <thread>
#include <mutex>
#include <pthread.h>

namespace lockless {

namespace {

/******************************************************************************/
/* DATA STRUCTS                                                               */
/******************************************************************************/

struct Epoch
{
    Epoch() : count(0) {}

    size_t count;
    List<DeferFn> deferList;
};

struct Epochs : array<Epoch, 2>
{
    Epochs()
    {
        for (auto& epoch : *this) epoch = Epoch();
    }
};

struct GlobalRcuImpl
{
    GlobalRcuImpl() : refCount(0), lock() {}

    Lock lock;
    size_t refCount;

    size_t epoch;

    List<Epochs> threadList;
    ListNode<Epochs>* gcDump; // Used to gather the defer list of dead threads.

    DebuggingLog<10240, DebugRcu>::type log;
} gRcu;


/******************************************************************************/
/* TLS                                                                        */
/******************************************************************************/

LOCKLESS_TLS ListNode<Epochs>* nodeTls;

void destructTls(void*)
{
    if (!nodeTls) return;

    for (size_t i = 0; i < 2; ++i)
        locklessCheckEq((*nodeTls)[i].count, 0, gRcu.log);

    // We can't delete the node here without racing with a gc op.
    nodeTls->mark();
}

Epochs& getTls()
{
    if (nodeTls) return *nodeTls;

    nodeTls = new ListNode<Epochs>();
    pthread_key_create(nullptr, &destructTls);

    gRcu.threadList.push(nodeTls);
    return *nodeTls;
}

} // namespace anonymous


/******************************************************************************/
/* GLOBAL RCU                                                                 */
/******************************************************************************/

GlobalRcu::
GlobalRcu()
{
    // Doing this lockless would be a pain and would be purty useless. We're
    // talking about speculatively spinnning up threads to then destroy them
    // when the cas fails.
    LockGuard<Lock> guard(gRcu.lock);

    gRcu.refCount++;
    if (gRcu.refCount > 1) return;

    locklessCheck(threadList.empty(), gRcu.log);

    gRcu.epoch = 1;
    gRcu.gcDump = new ListNode<Epochs>();
    gRcu.threadList.push(gRcu.gcDump);
}

GlobalRcu::
~GlobalRcu()
{
    LockGuard<Lock> guard(gRcu.lock);

    gRcu.refCount--;
    if (gRcu.refCount > 0) return;

    locklessCheck(threadList.empty(), gRcu.log);
    delete gcDump;
}

size_t
GlobalRcu::
enter()
{
    getTls()[gRcu.epoch & 1]++;

    // Prevents reads from being taking place before we increment the epoch
    // counter.
    atomic_thread_fence(memory_order_acquire);
}

void
GlobalRcu::
exit(size_t epoch)
{
    // Ensures that all reads are terminated before we decrement the epoch
    // counter. Unfortunately there's no equivalent of the release semantic for
    // reads so we need to use a full barrier instead. Sucky but it's life.
    atomic_thread_fence(memory_order_seq_cst);

    getTls()[epoch & 1].count--;
}

void
GlobalRcu::
defer(DeferList<DeferFn>* node)
{
    getTls()[gRcu.epoch & 1].deferList.push(node);
}


namespace {

ListNode<Epochs>*
deleteMarkedNode(
        ListNode<Epochs>* node,
        ListNode<Epochs>* prev)
{
    // Move all leftover defer work to the gcDump node which will be gc-ed on
    // the next successful gc pass for that epoch.
    for (size_t i = 0; i < 2; ++i) {
        Epoch& nodeEpoch = (*node)[i];
        locklessCheckEq(nodeEpoch.count, 0, gRcu.log);

        Epoch& gcEpoch = (*gRcu->gcDump)[i];
        gcEpoch.deferList.push(std::move(nodeEpoch.deferList));
    }

    auto* next = node->next();

    // If we're removing at the head, we need to use a safe approach in
    // case a new node was pushed while we were not looking.
    if (!prev) gRcu.threadList.remove(node);

    // Otherwise, we can safely remove it the simple way because only
    // one thread can ever be crawling the list.
    else prev->next(next);

    delete node;
    return next;
}

void execute(List<DeferFn>& target)
{
    ListNode<DeferFn>* node = target.head.exchange(nullptr);

    while (node) {
        // exec the defered work.
        (*node)();

        ListNode<DeferFn>* next = node->next();
        delete node;
        node = next;
    }
}

} // namespace anonymous

bool
GlobalRcu::
gc()
{
    // Pointless to have more then 2 threads gc-ing and limitting to one
    // significantly simplifies the algo.
    TryLockGuard<Lock> guard(gRcu.lock);
    if (!guard) return false;

    size_t epoch = (gRcu.epoch - 1) & 1;

    ListNode<Epochs>* node = threadList.head;
    ListNode<Epochs>* prev = nullptr;
    locklessCheckNe(node, nullptr, gRcu.log);

    // Do an initial pass over the list to see if we can do a gc pass.
    while (node) {

        // Dead node, get rid of it.
        if (node->isMarked()) {
            node = deleteMarkedNode(node, prev);
            continue;
        }

        // Someone's still in the epoch so we can't gc anything; just bail.
        if ((*node)[epoch].count) return false;

        prev = node;
        node = node->next();
    }


    node = gRcu.threadList.head;
    locklessCheckNe(node, nullptr, gRcu.log);

    // It's safe to start modifying the gc lists so start doing just that.
    while (node) {
        Epoch& nodeEpoch = (*node)[i];
        locklessCheckEq(nodeEpoch.count, 0, gRcu.log);

        execute(nodeEpoch.deferList);

        node = node->next();
    }

    // Complete all defered work before moving the epoch forward.
    atomic_thread_fence(memory_order_seq_cst);

    gRcu.epoch++;
    return true;
}


LogAggregator
GlobalRcu::
log()
{
    return LogAggregator(gRcu.log);
}


} // lockless
