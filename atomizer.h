/** atomizer.h                                 -*- C++ -*-
    Rémi Attab (remi.attab@gmail.com), 08 Dec 2012
    FreeBSD-style copyright and disclaimer apply

    Turns any given type into an atomically manipulable type.
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
struct Atomizer {};

template<typename T>
struct Atomizer<T, true>
{
    typedef uint64_t type;

    static type alloc(T value)
    {
        Converter<T> conv;
        conv.value = value;
        return conv.atom;
    }

    static T load(type atom)
    {
        Converter<T> conv;
        conv.atom = atom;
        return conv.value;
    }

    static void dealloc(type atom) {}

private:

    Atomizer()
    {
        std::assert(std::atomic<type>().is_lock_free());
        assert(std::atomic<type>().is_lock_free());
    }

    // \todo Need to make sure that value is right aligned on atom.
    // Otherwise the default MagicValues will clash on little-endian arch.
    struct Converter
    {
        union {
            T value;
            uint64_t atom;
        };
    };
};

template<typename T>
struct Atomizer<T, false>
{
    typedef T* type;

    static type alloc(const T& value)
    {
        return new T(value);
    }

    static type alloc(T&& value)
    {
        return new T(std::forward(value));
    }

    static T load(type atom)
    {
        return *atom;
    }

    static void dealloc(type atom) { delete T; }

private:

    Atomizer()
    {
        std::assert(std::atomic<type>().is_lock_free());
    }

};


} // namespace details


/******************************************************************************/
/* MAGIC VALUE                                                                */
/******************************************************************************/

/* Overide to provide whatever mask is appropriate for your type's magic value.

 */
template<typename T>
struct MagicValue
{
    // Grab the most-significant bits which are unlikely to be used.
    static T mask0 = 1ULL << 63;
    static T mask1 = 1ULL << 62;
};

template<typename T>
struct MagicValue<T*>
{
    // Used the least-significant bits of the pointers.
    // Assumes 64-bit alignmen -> DON'T USE ON char* !
    static T* mask0 = reinterpret_cast<T*>(1);
    static T* mask1 = reinterpret_cast<T*>(2);
};

template<typename T, typename Magic = MagicValue<T> >
bool isMagicValue0(T value)
{
    return value & Magic::mask0;
}

template<typename T, typename Magic = MagicValue<T> >
bool isMagicValue1(T value)
{
    return value & Magic::mask1;
}

template<typename T, typename Magic = MagicValue<T> >
bool isMagicValue(T value)
{
    return isMagicValue0<T, Magic>(value) || isMagicValue1<T, Magic>(value);
}

} // namespace lockless

#endif // __lockless_utils_h__
