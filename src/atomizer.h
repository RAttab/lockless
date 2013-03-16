/** atomizer.h                                 -*- C++ -*-
    Rémi Attab (remi.attab@gmail.com), 08 Dec 2012
    FreeBSD-style copyright and disclaimer apply

    Turns any given type into an atomically manipulable type.
*/

#ifndef __lockless_utils_h__
#define __lockless_utils_h__

#include <cstdint>
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
    enum { value = sizeof(T) <= sizeof(size_t) };
};

template<>
struct IsAtomic<std::string>
{
    enum { value = false };
};


/******************************************************************************/
/* VALUE TRAITS                                                               */
/******************************************************************************/

template<typename T, bool isAtomic = IsAtomic<T>::value>
struct Atomizer {};

template<typename T>
struct Atomizer<T, true>
{
    typedef size_t type;

    static type alloc(T value)
    {
        Converter conv;
        conv.value = value;
        return conv.atom;
    }

    static T load(type atom)
    {
        Converter conv;
        conv.atom = atom;
        return conv.value;
    }

    static void dealloc(type) {}

private:

    // \todo Need to make sure that value is right aligned on atom.
    // Otherwise the default MagicValues will clash on little-endian arch.
    struct Converter
    {
        union {
            T value;
            type atom;
        };
    };
};


template<typename T>
struct Atomizer<T, false>
{
    typedef uintptr_t type;

    static type alloc(const T& value)
    {
        return reinterpret_cast<type>(new T(value));
    }

    template<typename V>
    static type alloc(V&& value)
    {
        return reinterpret_cast<type>(new T(std::forward<V>(value)));
    }

    static T load(type atom)
    {
        return *reinterpret_cast<T*>(atom);
    }

    static void dealloc(type atom)
    {
        delete reinterpret_cast<T*>(atom);
    }
};


} // namespace details
} // namespace lockless

#endif // __lockless_utils_h__
