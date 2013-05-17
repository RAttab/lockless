/* grcu.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 01 Apr 2013
   FreeBSD-style copyright and disclaimer apply

   Static Read-Copy-Update gRcuementation.
*/

#define LOCKLESS_RCU_DEBUG 1
#define LOCKLESS_CHECK_ABORT 1

#include "grcu.h"
#include "tls.h"
#include "tm.h"
#include "log.h"
#include "check.h"

#include <array>
#include <algorithm>
#include <thread>
#include <mutex>
#include <memory>

using namespace std;
using namespace lockless;

namespace {

bool gc();
string print();

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


/******************************************************************************/
/* GRCU                                                                       */
/******************************************************************************/

struct GlobalRcuImpl
{
    GlobalRcuImpl() : refCount(0)  {}

    Lock refLock;
    size_t refCount;

    size_t epoch;
    List<Epochs> threadList;

    Lock gcLock;
    ListNode<Epochs>* gcDump; // Used to gather the defer list of dead threads.

    DebuggingLog<10240, DebugRcu>::type log;
} gRcu;


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

bool gc()
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


string print()
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
    LockGuard<Lock> guard(gRcu.refLock);

    if (!gRcu.refCount) {
        // Make sure everything was properly cleaned up by the GlobalRcu
        // destructor.
        for (size_t i = 0; i < 2; ++i)
            locklessCheckEq(node.get()[i].count, 0ULL, gRcu.log);
    }
    else {
        // Move all leftover defer work to the gcDump node which will be gc-ed
        // on the next successful gc pass for that epoch.
        for (size_t i = 0; i < 2; ++i) {
            Epoch& nodeEpoch = node.get()[i];
            locklessCheckEq(nodeEpoch.count, 0ULL, gRcu.log);

            Epoch& gcEpoch = gRcu.gcDump->get()[i];
            gcEpoch.deferList.take(nodeEpoch.deferList);
        }
    }

    gRcu.threadList.remove(&node);

    /* Since we removed our node from the list we know that no new gc pass will
       be able to read our node so all we need to do is wait for the current gc
       pass to complete to know that it's safe to delete our node.

       In a way, it's a bit like a pseudo-rcu.
    */
    LockGuard<Lock> waitForGc(gRcu.gcLock);
}

// Direct access to a node for a given thread.
Tls<ListNode<Epochs>, GlobalRcuImpl> nodeTls(&constructTls, &destructTls);

Epochs& getTls() { return nodeTls.get(); }

} // namespace anonymous


/******************************************************************************/
/* GLOBAL RCU                                                                 */
/******************************************************************************/

namespace lockless {

GlobalRcu::
GlobalRcu()
{
    // Doing this lockless would be a pain and would be purty useless.
    LockGuard<Lock> guard(gRcu.refLock);

    gRcu.refCount++;
    if (gRcu.refCount > 1) return;

    gRcu.epoch = 1;
    gRcu.gcDump = new ListNode<Epochs>();
    gRcu.threadList.push(gRcu.gcDump);
}

GlobalRcu::
~GlobalRcu()
{
    LockGuard<Lock> guard(gRcu.refLock);

    gRcu.refCount--;
    if (gRcu.refCount > 0) return;

    // Run any leftover defered work in both epochs.
    size_t epoch = gRcu.epoch;
    ::gc();
    ::gc();

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


bool
GlobalRcu::
gc()
{
    // Pointless to have more then 2 threads gc-ing and limitting to one
    // significantly simplifies the algo.
    TryLockGuard<Lock> guard(gRcu.gcLock);
    if (!guard) return false;

    return ::gc();
}

string
GlobalRcu::
print() const
{
    return ::print();
}

LogAggregator
GlobalRcu::
log()
{
    return LogAggregator(gRcu.log);
}


/******************************************************************************/
/* GC THREAD                                                                  */
/******************************************************************************/

namespace {

struct GcThreadImpl
{
    GcThreadImpl() : refCount(0), shutdown(true) {}

    Lock lock;
    size_t refCount;

    atomic<bool> shutdown;
    unique_ptr<thread> gcThread;

    DebuggingLog<10240, DebugRcu>::type log;

} gcThread;

void doGcThread()
{
    enum { MaxSleepMs = 1000 };
    size_t sleepMs = 1;

    auto& log = gcThread.log;

    log.log(LogRcu, "gc-start", "%lf", Time::wall());

    GlobalRcu rcu;
    while (!gcThread.shutdown) {
        Timer tm;
        bool success = rcu.gc();

        if (!success)
            sleepMs = min<size_t>(sleepMs * 2, MaxSleepMs);
        else sleepMs = sleepMs - 1;

        log.log(LogRcu, "gc", "%lf - duration=%lf, sleep=%ld",
                Time::wall(), tm.elapsed(), sleepMs);

        if (!sleepMs) sleepMs = 1;
        else this_thread::sleep_for(chrono::milliseconds(sleepMs));
    }

    log.log(LogRcu, "gc-end", "%lf", Time::wall());
}

} // namespace anonymous

GcThread::
GcThread() : joined(false)
{
    lock_guard<Lock> guard(gcThread.lock);
    if (++gcThread.refCount > 1) return;

    gcThread.shutdown = false;
    gcThread.gcThread.reset(new thread(doGcThread));
}

void
GcThread::
join()
{
    if (joined) return;
    joined = true;

    lock_guard<Lock> guard(gcThread.lock);
    locklessCheck(!gcThread.shutdown, gcThread.log);
    if (--gcThread.refCount > 0) return;

    gcThread.shutdown = true;
    gcThread.gcThread->join();
    gcThread.gcThread.reset();
}

void
GcThread::
detach()
{
    if (joined) return;
    joined = true;

    lock_guard<Lock> guard(gcThread.lock);
    locklessCheck(!gcThread.shutdown, gcThread.log);
    if (--gcThread.refCount > 0) return;

    gcThread.shutdown = true;
    gcThread.gcThread->detach();
    gcThread.gcThread.reset();
}

LogAggregator
GcThread::
log()
{
    return LogAggregator(gcThread.log);
}

} // lockless
