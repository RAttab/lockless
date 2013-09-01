/* seq_lock.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 24 Aug 2013
   FreeBSD-style copyright and disclaimer apply

   SeqLock which uses a sequence number to determin whether a write op occured
   during a read op. This does require the reader to restart the read op.
*/

#ifndef __lockless__seq_lock_h__
#define __lockless__seq_lock_h__

#include "lock.h"

#include <atomic>
#include <cassert>

namespace lockless {


/******************************************************************************/
/* SEQ LOCK                                                                   */
/******************************************************************************/

/** Usage example:

    typedef SeqLock<FairLock> Lock;
    Lock lock;
    {
        LockGuard<Lock> guard(lock);
        doWrite();
    }
    read(lock, [&] { doRead(); });
    int val = readRet<int>(lock, [] { return doRead(); });

    Guarantees that the last call to read() that occurs before readGuard returns
    will not have been interupted by a write.

    Note that read() may be called multiple times if there are ongoing writes.
    Otherwise, the overhead for doing a read operation should be minimal.
 */
template<typename LockT>
struct SeqLock : public Lock
{
    SeqLock() : seq(0) {}

    void lock()
    {
        // We can't have multiple writes because if two writes nest and a read
        // nests within that then there's no way to detect the writes.
        writeLock.lock();

        // Since we're still holding the lock, we don't need an atomic inc.
        // We do need the atomic store with the associated barriers though.
        seq = seq + 1;
    }

    bool tryLock()
    {
        if (!writeLock.tryLock()) return false;

        seq = seq + 1;
        return true;
    }

    void unlock()
    {
        // Since we're still holding the lock, we don't need an atomic inc.
        // We do need the atomic store with the associated barriers though.
        seq = seq + 1;

        writeLock.unlock();
    }

    /** Defines the boundary of a read operation. */
    size_t begin() const
    {
        size_t ret;

        // This is an optimization that moves the odd check from commit to begin
        // which will avoid doing read operations that can't succeed.
        while ((ret = seq) & 1);

        return ret;
    }

    /** Ends a read operation and detect whether a write took place in the
        meantime.
     */
    bool commit(size_t old) const
    {
        // An odd value means that a write was ongoing.
        // Unequal values means that a write intercutted our
        return seq == old;
    }

private:
    std::atomic<size_t> seq;
    LockT writeLock;
};


/******************************************************************************/
/* READ                                                                       */
/******************************************************************************/

template<typename Lock, typename Fn>
void read(Lock& lock, const Fn& fn)
{
    size_t seq;
    do {
        seq = lock.begin();
        fn();
    } while(!lock.commit(seq));
}

// The Ret template param must be specified
template<typename Ret, typename Lock, typename Fn>
Ret readRet(Lock& lock, const Fn& fn)
{
    size_t seq;
    Ret ret;

    do {
        seq = lock.begin();
        ret = fn();
    } while(!lock.commit(seq));

    return ret;
}

} // lockless

#endif // __lockless__seq_lock_h__
