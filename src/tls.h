/* tls.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 14 Apr 2013
   FreeBSD-style copyright and disclaimer apply

   Thread local storage helper class.
*/

#ifndef __lockless__tls_h__
#define __lockless__tls_h__

#include <pthread.h>
#include <functional>

namespace lockless {


/******************************************************************************/
/* MACRO                                                                      */
/******************************************************************************/

// Thread local storage.
#define LOCKLESS_TLS __thread __attribute__(( tls_model("initial-exec") ))


/******************************************************************************/
/* TLS                                                                        */
/******************************************************************************/

/**

   \todo Need to make sure that this is actually faster then a straight up
   pthread_getspecific and pthread_setspecific. If that's not the case then we
   should just wrap these functions.
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

private:

    void init() const
    {
        value = new T();
        if (constructFn) constructFn(*value);

        // use pthread TLS for the destructFn.
        pthread_key_create(&key, &destructor);
        pthread_setspecific(key, this);
    }

    Fn constructFn;
    Fn destructFn;

    static void destructor(void* obj)
    {
        Tls<T, Tag>* this_ = static_cast<Tls<T, Tag>*>(obj);

        if (this_->destructFn) this_->destructFn(this_->get());
        delete this_->value;

        pthread_key_delete(key);
    }

    static LOCKLESS_TLS T* value;
    static LOCKLESS_TLS pthread_key_t key;
};

template<typename T, typename Tag>
LOCKLESS_TLS T* Tls<T, Tag>::value = nullptr;

template<typename T, typename Tag>
LOCKLESS_TLS pthread_key_t Tls<T, Tag>::key;

} // lockless

#endif // __lockless__tls_h__
