/* tls.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 14 Apr 2013
   FreeBSD-style copyright and disclaimer apply

   Thread local storage helper class.
*/

#ifndef __lockless__tls_h__
#define __lockless__tls_h__

#include "arch.h"

#include <pthread.h>
#include <functional>
#include <cassert>

namespace lockless {


/******************************************************************************/
/* MACRO                                                                      */
/******************************************************************************/

// Thread local storage.
#define locklessTls __thread __attribute__(( tls_model("initial-exec") ))


/******************************************************************************/
/* THREAD ID                                                                  */
/******************************************************************************/

/* Returns a unique identifier for the current thread. */
size_t threadId();


/******************************************************************************/
/* TLS                                                                        */
/******************************************************************************/

/** Thread local storage that provides both a constructor and destructor
    callback.

    The Tag template parameter is used to force seperate static TLS storage to
    be initialized if two instances of the Tls template are created with for the
    same type. For Example:

        Tls<size_t, int> tls1;
        tls1 = 10;

        struct Tag;
        Tls<size_t, Tag> tls2;
        tls2 = 20;

        Tls<size_t, int> tls3;
        tls3 = 30;

        assert(tls1 == tls3);
        assert(tls2 == 20);

    \todo The static storage sticks around even after the originating object
    goes out of scope. We should probably create a list of all TLS storage
    created by the object and destroyed them when we go out of scope.
 */
template<typename T, typename Tag>
struct Tls
{
    typedef std::function<void(T&)> Fn;

    Tls(const Fn& constructFn = Fn(), const Fn& destructFn = Fn()) :
        constructFn(constructFn),
        destructFn(destructFn)
    {}

    T& get()
    {
        if (!value) init();
        return *value;
    }

    const T& get() const
    {
        if (!value) init();
        return *value;
    }

    T& operator*() { return get(); }
    const T& operator*() const { return get(); }

    T* operator->() { return &get(); }
    const T* operator->() const { return &get(); }


    void reset()
    {
        if (!value) return;
        destructor(static_cast<Fn*>(pthread_getspecific(key)));
    }

private:

    void init() const locklessNeverInline
    {
        value = new T();
        if (constructFn) constructFn(*value);

        // use pthread TLS for the destructFn.
        pthread_key_create(&key, &destructor);
        pthread_setspecific(key, new Fn(destructFn));
    }

    static void destructor(void* obj)
    {
        destructor(static_cast<Fn*>(obj));
    }

    static void destructor(Fn* destructFn)
    {
        assert(destructFn);
        if (*destructFn) (*destructFn)(*value);

        pthread_key_delete(key);
        delete value;
        value = nullptr;
    }

    Fn constructFn;
    Fn destructFn;

    static locklessTls T* value;
    static locklessTls pthread_key_t key;
};

template<typename T, typename Tag>
locklessTls T* Tls<T, Tag>::value = nullptr;

template<typename T, typename Tag>
locklessTls pthread_key_t Tls<T, Tag>::key;

} // lockless

#endif // __lockless__tls_h__
