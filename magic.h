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
/* MAGIC VALUE                                                                */
/******************************************************************************/

/* Can be overidden by the user for a given type. */
template<typename T> struct MagicValue {};


/* Grab the most-significant bits which are unlikely to be used. */
template<>
struct MagicValue<size_t>
{
    static T mask0 = 1ULL << 63;
    static T mask1 = 1ULL << 62;
};


/* Used the least-significant bits of the pointers. Assumes 4-byte alignment or
   more.
 */
template<typename T>
struct MagicValue<T*>
{
    static T* mask0 = reinterpret_cast<T*>(1);
    static T* mask1 = reinterpret_cast<T*>(2);
};

/* Since we can't steal alignment bits, just great the most-significant bits. */
template<>
struct MagicValue<char*>
{
    static T* mask0 = reinterpret_cast<T*>(1) << (sizeof(T*) * 8 - 1);
    static T* mask1 = reinterpret_cast<T*>(1) << (sizeof(T*) * 8 - 2);
};

} // lockless

#endif // __lockless__magic_h__
