/* tls.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 14 Apr 2013
   FreeBSD-style copyright and disclaimer apply

   Thread local storage helper class.
*/

#ifndef __lockless__tls_h__
#define __lockless__tls_h__

#include <pthread.h>
#include <functional>
#include <cassert>

namespace lockless {


/******************************************************************************/
/* MACRO                                                                      */
/******************************************************************************/

// Thread local storage.
#define LOCKLESS_TLS __thread __attribute__(( tls_model("initial-exec") ))


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

    \todo Need to make sure that using __thread is actually faster then a
    straight up pthread tls. If that doesn't pan out just wrap pthread tls.
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

    operator T& () { return get(); }
    operator const T& () const { return get(); }

    template<typename TT>
    void set(TT&& other)
    {
        get() = std::forward<TT>(other);
    }

    template<typename TT>
    Tls<T, Tag>& operator= (TT&& other)
    {
        set(std::forward<TT>(other));
        return *this;
    }

    void reset()
    {
        if (!value) return;
        destructor(static_cast<Fn*>(pthread_getspecific(key)));
    }

private:

    void init() const
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

    static LOCKLESS_TLS T* value;
    static LOCKLESS_TLS pthread_key_t key;
};

template<typename T, typename Tag>
LOCKLESS_TLS T* Tls<T, Tag>::value = nullptr;

template<typename T, typename Tag>
LOCKLESS_TLS pthread_key_t Tls<T, Tag>::key;

} // lockless

#endif // __lockless__tls_h__
