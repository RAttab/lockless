/* bits.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 15 Jun 2013
   FreeBSD-style copyright and disclaimer apply

   Bit manipulation header
*/

#ifndef __lockless__bits_h__
#define __lockless__bits_h__

namespace lockless {

inline size_t clz(unsigned x)           { return __builtin_clz(x); }
inline size_t clz(unsigned long x)      { return __builtin_clzl(x); }
inline size_t clz(unsigned long long x) { return __builtin_clzll(x); }

inline size_t ctz(unsigned x)           { return __builtin_ctz(x); }
inline size_t ctz(unsigned long x)      { return __builtin_ctzl(x); }
inline size_t ctz(unsigned long long x) { return __builtin_ctzll(x); }

inline size_t pop(unsigned x)           { return __builtin_popcount(x); }
inline size_t pop(unsigned long x)      { return __builtin_popcountl(x); }
inline size_t pop(unsigned long long x) { return __builtin_popcountll(x); }

template<typename Int>
size_t log2(Int x)
{
    return (sizeof(x) * 8) - clz(x);
}


} // lockless

#endif // __lockless__bits_h__
