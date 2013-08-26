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

    UnfairLock refLock;
    size_t refCount;

    atomic<size_t> epoch;
    List<Epochs> threadList;

    UnfairLock gcLock;
    ListNode<Epochs>* gcDump; // Used to gather the defer list of dead threads.

    DebuggingLog<10240, DebugRcu>::type log;
} gRcu;


void execute(List<GlobalRcu::DeferFn>& deferList)
{
    auto* node = deferList.head.exchange(nullptr);

    while (node) {
        // exec the defered work.
        node->value();

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
        if (node->value[epoch].count) return false;

        node = node->next();
    }

    // Our epoch has been fully vacated so time to execute defered work.
    node = gRcu.threadList.head;
    while (node) {
        Epoch& nodeEpoch = node->value[epoch];
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
        Epochs& epochs = node->value;
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
            gRcu.threadList.head.load(), gRcu.gcDump, gRcu.refCount,
            gRcu.epoch.load(), epochsTotal[0], epochsTotal[1]);

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
    LockGuard<UnfairLock> guard(gRcu.refLock);

    if (!gRcu.refCount) {
        // Make sure everything was properly cleaned up by the GlobalRcu
        // destructor.
        for (size_t i = 0; i < 2; ++i)
            locklessCheckEq(node.value[i].count, 0ULL, gRcu.log);
    }
    else {
        // Move all leftover defer work to the gcDump node which will be gc-ed
        // on the next successful gc pass for that epoch.
        for (size_t i = 0; i < 2; ++i) {
            Epoch& nodeEpoch = node.value[i];
            locklessCheckEq(nodeEpoch.count, 0ULL, gRcu.log);

            Epoch& gcEpoch = gRcu.gcDump->value[i];
            gcEpoch.deferList.take(nodeEpoch.deferList);
        }
    }

    gRcu.threadList.remove(&node);

    /* Since we removed our node from the list we know that no new gc pass will
       be able to read our node so all we need to do is wait for the current gc
       pass to complete to know that it's safe to delete our node.

       In a way, it's a bit like a pseudo-rcu.
    */
    LockGuard<UnfairLock> waitForGc(gRcu.gcLock);
}

// Direct access to a node for a given thread.
Tls<ListNode<Epochs>, GlobalRcuImpl> nodeTls(&constructTls, &destructTls);

Epochs& getTls() { return *nodeTls; }

} // namespace anonymous


/******************************************************************************/
/* GLOBAL RCU                                                                 */
/******************************************************************************/

namespace lockless {

GlobalRcu::
GlobalRcu()
{
    // Doing this lockless would be a pain and would be purty useless.
    LockGuard<UnfairLock> guard(gRcu.refLock);

    gRcu.refCount++;
    if (gRcu.refCount > 1) return;

    gRcu.epoch = 1;
    gRcu.gcDump = new ListNode<Epochs>();
    gRcu.threadList.push(gRcu.gcDump);
}

GlobalRcu::
~GlobalRcu()
{
    LockGuard<UnfairLock> guard(gRcu.refLock);

    gRcu.refCount--;
    if (gRcu.refCount > 0) return;

    // Run any leftover defered work in both epochs.
    size_t epoch = gRcu.epoch;
    ::gc();
    ::gc();

    // Executing gc() twice should increment the epoch counter twice. If this
    // check fails then there's still a thread in one of the epochs.
    locklessCheckEq(epoch + 2, gRcu.epoch.load(), gRcu.log);

    bool removed = gRcu.threadList.remove(gRcu.gcDump);
    locklessCheck(removed, gRcu.log);
    delete gRcu.gcDump;
}

size_t
GlobalRcu::
enter()
{
    while (true) {
        size_t epoch = gRcu.epoch;
        getTls()[epoch & 1].count++;

        // Prevents reads from taking place before we increment the epoch
        // counter.
        atomic_thread_fence(memory_order_acquire);

        /* Fun scenario that we need to guard against:

           1) Read the gRcu.epoch E and gets pre-empted.
           2) gRcu.epoch is moved forward such that epoch E is available for gc.
           3) First pass of the gc thread finds the epoch vacated.
           4) Our thread wakes up and increments epoch E and exits.
           5) Another thread increments epoch E+1 and exits.
           6) Assuming no asserts, gc thread moves gRcu.epoch forward.

           This is an issue because our first thread essentially entered E+2
           even though a later thread entered epoch E+1. This means our first
           thread in E+2 will not be taken into account while gc-ing the epoch
           E+1 even though it entered before the second thread that is in E+1.
           In a nutshell, this breaks the RCU guarantee which is bad(tm).

           The fix is quite simple, make sure that gRcu.epoch hasn't been moved
           foward before exiting. While there are probably cleaner solutions,
           the ones I can think of requrie the introduction of a CAS which would
           limit the scalability of GlobalRcu. Also, gRcu.epoch shouldn't be
           moved forward too often (every 1ms is reasonable enough) so this
           branch should fail very rarely.
        */
        if ((epoch & 1) == (gRcu.epoch & 1)) return epoch;

        getTls()[epoch & 1].count--;
    }
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
    TryLockGuard<UnfairLock> guard(gRcu.gcLock);
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

    UnfairLock lock;
    size_t refCount;

    atomic<bool> shutdown;
    unique_ptr<thread> gcThread;

    DebuggingLog<10240, DebugRcu>::type log;

} gcThread;

void doGcThread()
{
    locklessEnum size_t MaxSleepMs = 1000;
    size_t sleepMs = 1;

    auto& log = gcThread.log;

    log(LogRcu, "gc-start", "%lf", wall());

    GlobalRcu rcu;
    while (!gcThread.shutdown) {
        Timer<Wall> tm;
        bool success = rcu.gc();

        if (!success)
            sleepMs = min<size_t>(sleepMs * 2, MaxSleepMs);
        else sleepMs = sleepMs - 1;

        log(LogRcu, "gc", "%lf - duration=%lf, sleep=%ld",
                wall(), tm.elapsed(), sleepMs);

        if (!sleepMs) sleepMs = 1;
        else this_thread::sleep_for(chrono::milliseconds(sleepMs));
    }

    log(LogRcu, "gc-end", "%lf", wall());
}

} // namespace anonymous

GcThread::
GcThread() : joined(false)
{
    lock_guard<UnfairLock> guard(gcThread.lock);
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

    lock_guard<UnfairLock> guard(gcThread.lock);
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

    lock_guard<UnfairLock> guard(gcThread.lock);
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
