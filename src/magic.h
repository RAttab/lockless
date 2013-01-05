/** magic.h                                 -*- C++ -*-
    Rémi Attab (remi.attab@gmail.com), 20 Dec 2012
    FreeBSD-style copyright and disclaimer apply

    Struct to determine whether a given value is magic (ie. has a special
    meaning).

*/

#ifndef __lockless__magic_h__
#define __lockless__magic_h__

#include <cstdint>

namespace lockless {


/******************************************************************************/
/* UTILS                                                                      */
/******************************************************************************/

namespace details {

template<typename T>
constexpr T msbMask(unsigned pos)
{
    return T(1ULL << ((sizeof(T) * 8) - (1 + pos)));
}

} // namespace details


/******************************************************************************/
/* MAGIC VALUE                                                                */
/******************************************************************************/

/* Can be overidden by the user for a given type. */
template<typename T> struct MagicValue {};


/* Grab the most-significant bits which are unlikely to be used. */
template<>
struct MagicValue<size_t>
{
    static constexpr size_t mask0 = details::msbMask<size_t>(0);
    static constexpr size_t mask1 = details::msbMask<size_t>(1);
};


/* Used the least-significant bits of the pointers. Assumes 4-byte alignment or
   more.
 */
template<typename T>
struct MagicValue<T*>
{
    static constexpr T* mask0 = reinterpret_cast<T*>(1);
    static constexpr T* mask1 = reinterpret_cast<T*>(2);
};


/* Since we can't steal alignment bits, just great the most-significant bits. */
template<>
struct MagicValue<char*>
{
    static constexpr char* mask0 = details::msbMask<char*>(0);
    static constexpr char* mask1 = details::msbMask<char*>(1);
};

} // lockless

#endif // __lockless__magic_h__
