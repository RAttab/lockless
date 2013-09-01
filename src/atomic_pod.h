/* atomic_pod.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 01 Jul 2013
   FreeBSD-style copyright and disclaimer apply

   std::atomic but pod-ed.

   Yes this is not a great idea but I sorta ran out of options when trying to do
   manual padding of a struct with atomic variables in it. See the giant wall of
   text for the Pad class for more details on why.

 */

#ifndef __lockless__atomic_pod_h__
#define __lockless__atomic_pod_h__

#include "utils.h"

namespace lockless {


/******************************************************************************/
/* ATOMIC                                                                     */
/******************************************************************************/

// Really should need anything but this so let's not bother for now.
locklessEnum int AtomicSeqCst = __ATOMIC_SEQ_CST;


/** C++11 atomics are nice and all but they're not POD which sucks. The c11
    atomics don't have that issue but gcc 4.7 doesn't implement them so we'll
    just have to make our own...
 */
template<typename T>
struct AtomicPod
{
    T value;

    T load() const { return __atomic_load_n(&value, AtomicSeqCst); }
    operator T () const { return load(); }

    void store(T v)
    {
        __atomic_store_n(&value, v, AtomicSeqCst);
    }
    AtomicPod<T>& operator= (T v)
    {
        store(v);
        return *this;
    }

    T exchange(T v)
    {
        return __atomic_exchange_n(&value, v, AtomicSeqCst);
    }

    bool compare_exchange_strong(T& expected, T v)
    {
        return __atomic_compare_exchange_n(
                &value, &expected, v, false, AtomicSeqCst, AtomicSeqCst);
    }

    bool compare_exchange_weak(T& expected, T v)
    {
        return __atomic_compare_exchange_n(
                &value, &expected, v, false, AtomicSeqCst, AtomicSeqCst);
    }

#define LOCKLESS_ATOMIC_POD_BINARY_OP(op,name)                          \
    T operator op (T v)                                                 \
    {                                                                   \
        return __atomic_ ## name ## _fetch(&value, v, AtomicSeqCst);    \
    }                                                                   \
    T fetch_ ## name (T v)                                              \
    {                                                                   \
        return __atomic_fetch_ ## name (&value, v, AtomicSeqCst);       \
    }

    LOCKLESS_ATOMIC_POD_BINARY_OP(+=,add)
    LOCKLESS_ATOMIC_POD_BINARY_OP(-=,sub)
    LOCKLESS_ATOMIC_POD_BINARY_OP(|=,or)
    LOCKLESS_ATOMIC_POD_BINARY_OP(&=,and)
    LOCKLESS_ATOMIC_POD_BINARY_OP(^=,xor)


#define LOCKLESS_ATOMIC_POD_UNARY_OP(op,name)                           \
    T operator op ()                                                    \
    {                                                                   \
        return __atomic_ ## name ## _fetch (&value, 1, AtomicSeqCst);   \
    }                                                                   \
    T operator op (int)                                                 \
    {                                                                   \
        return __atomic_fetch_ ## name (&value, 1, AtomicSeqCst);       \
    }

    LOCKLESS_ATOMIC_POD_UNARY_OP(++,add)
    LOCKLESS_ATOMIC_POD_UNARY_OP(--,sub)

};


} // lockless

#endif // __lockless__atomic_pod_h__
