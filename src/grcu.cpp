/* grcu.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 01 Apr 2013
   FreeBSD-style copyright and disclaimer apply

   Static Read-Copy-Update gRcuementation.
*/

#include "grcu.h"
#include "tls.h"
#include "log.h"
#include "check.h"

#include <array>
#include <thread>
#include <mutex>
#include <pthread.h>

using namespace std;

namespace lockless {

namespace {

bool gcImpl();

/******************************************************************************/
/* DATA STRUCTS                                                               */
/******************************************************************************/

struct Epoch
{
    void init()
    {
        count = 0;
        deferList.head = nullptr;
    }

    size_t count;
    List<GlobalRcu::DeferFn> deferList;
};

struct Epochs : public array<Epoch, 2>
{
    Epochs()
    {
        for (auto& epoch : *this) epoch.init();
    }
};

struct GlobalRcuImpl
{
    GlobalRcuImpl() : lock(), refCount(0)  {}

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

void constructTls(ListNode<Epochs>& node)
{
    gRcu.threadList.push(&node);
}

void destructTls(ListNode<Epochs>& node)
{
    // Ensures that we don't race with the destruction of the gRcu lock.
    LockGuard<Lock> guard(gRcu.lock);

    // Everything should have been cleaned up by the gRcu destructor. Bail.
    if (!gRcu.refCount) {
        for (size_t i = 0; i < 2; ++i)
            locklessCheckEq(node.get()[i].count, 0ULL, gRcu.log);
        return;
    }

    // Move all leftover defer work to the gcDump node which will be gc-ed on
    // the next successful gc pass for that epoch.
    for (size_t i = 0; i < 2; ++i) {
        Epoch& nodeEpoch = node.get()[i];
        locklessCheckEq(nodeEpoch.count, 0ULL, gRcu.log);

        Epoch& gcEpoch = gRcu.gcDump->get()[i];
        gcEpoch.deferList.take(nodeEpoch.deferList);
    }

    gRcu.threadList.remove(&node);
}

// Direct access to a node for a given thread.
Tls<ListNode<Epochs>, GlobalRcuImpl> nodeTls(&constructTls, &destructTls);

Epochs& getTls() { return nodeTls.get(); }

} // namespace anonymous


/******************************************************************************/
/* GLOBAL RCU                                                                 */
/******************************************************************************/

GlobalRcu::
GlobalRcu()
{
    // Doing this lockless would be a pain and would be purty useless.
    LockGuard<Lock> guard(gRcu.lock);

    gRcu.refCount++;
    if (gRcu.refCount > 1) return;

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

    // Run any leftover defered work in both epochs.
    size_t epoch = gRcu.epoch;
    gcImpl();
    gcImpl();

    // Executing gc() twice should increment the epoch counter twice. If this
    // check fails then there's still a thread in one of the epochs.
    locklessCheckEq(epoch + 2, gRcu.epoch, gRcu.log);

    bool removed = gRcu.threadList.remove(gRcu.gcDump);
    locklessCheck(removed, gRcu.log);
    delete gRcu.gcDump;
}

size_t
GlobalRcu::
enter()
{
    size_t epoch = gRcu.epoch;
    getTls()[epoch & 1].count++;

    // Prevents reads from taking place before we increment the epoch counter.
    atomic_thread_fence(memory_order_acquire);

    return epoch;
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
defer(ListNode<DeferFn>* node)
{
    getTls()[gRcu.epoch & 1].deferList.push(node);
}


namespace {

void execute(List<GlobalRcu::DeferFn>& deferList)
{
    auto* node = deferList.head.exchange(nullptr);

    while (node) {
        // exec the defered work.
        node->get()();

        auto* next = node->next();
        delete node;
        node = next;
    }
}

bool gcImpl()
{
    size_t epoch = (gRcu.epoch - 1) & 1;

    ListNode<Epochs>* node = gRcu.threadList.head;
    locklessCheckNe(node, nullptr, gRcu.log);

    // Do an initial pass over the list to see if we can do a gc pass.
    while (node) {
        // Someone's still in the epoch so we can't gc anything; just bail.
        if (node->get()[epoch].count) return false;

        node = node->next();
    }

    // Our epoch has been fully vacated so time to execute defered work.
    node = gRcu.threadList.head;
    while (node) {
        Epoch& nodeEpoch = node->get()[epoch];
        locklessCheckEq(nodeEpoch.count, 0ULL, gRcu.log);

        execute(nodeEpoch.deferList);
        node = node->next();
    }

    // Complete all defered work before moving the epoch forward.
    atomic_thread_fence(memory_order_seq_cst);

    gRcu.epoch++;
    return true;
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

    return gcImpl();
}

string
GlobalRcu::
print() const
{
    size_t epochsTotal[2] = { 0, 0 };

    string line;
    ListNode<Epochs>* node = gRcu.threadList.head;

    while (node) {
        Epochs& epochs = node->get();
        line += format(
                "  ptr=%10p, next=%10p, count=[ %ld, %ld ], defer=[ %10p, %10p ]\n",
                node, node->next(), epochs[0].count, epochs[1].count,
                epochs[0].deferList.head.load(),
                epochs[1].deferList.head.load());

        for (size_t i = 0; i < 2; ++i)
            epochsTotal[i] += epochs[i].count;

        node = node->next();
    }

    string head = format(
            "head=%p, dump=%p, refCount=%ld, epoch=%ld, count=[ %ld, %ld ]\n",
            gRcu.threadList.head.load(), gRcu.gcDump, gRcu.refCount, gRcu.epoch,
            epochsTotal[0], epochsTotal[1]);

    return head + line;
}


LogAggregator
GlobalRcu::
log()
{
    return LogAggregator(gRcu.log);
}


} // lockless
