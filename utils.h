/** lockless_utils.h                                 -*- C++ -*-
    Rémi Attab, 08 Dec 2012
    Copyright (c) 2012 Datacratic.  All rights reserved.

    utilities for storing arbitrary values into an lockless container.

*/

#ifndef __lockless_utils_h__
#define __lockless_utils_h__

#include <cstdint>
#include <cassert>
#include <utility>
#include <atomic>

namespace lockless {
namespace details {

/******************************************************************************/
/* VALUE TRAITS                                                               */
/******************************************************************************/

template<typename T>
struct IsAtomic
{
    enum { value = sizeof(T) <= sizeof(uint64_t) };
};


/******************************************************************************/
/* VALUE TRAITS                                                               */
/******************************************************************************/

template<typename T, bool isAtomic = IsAtomic<T>::value>
struct Container {};

template<typename T>
struct Container<T, true>
{
    typedef uint64_t type;

    Container()
    {
        std::assert(std::atomic<type>().is_lock_free());
    }


    struct Converter
    {
        union {
            T value;
            type cont;
        };
    };

    static type alloc(T value)
    {
        Converter<T> conv;
        conv.value = value;
        return conv.cont;
    }

    static T load(type cont)
    {
        Converter<T> conv;
        conv.cont = cont;
        return conv.value;
    }

    static void dealloc(type cont) {}
};

template<typename T>
struct Container<T, false>
{
    typedef T* type;

    Container()
    {
        std::assert(std::atomic<type>().is_lock_free());
    }

    static type alloc(const T& value)
    {
        return new T(value);
    }

    static type alloc(T&& value)
    {
        return new T(std::forward(value));
    }

    static T load(type cont)
    {
        return *cont;
    }

    static void free(type cont) { delete T; }
};


} // namespace details
} // namespace lockless

#endif // __lockless_utils_h__
