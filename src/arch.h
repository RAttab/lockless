/* arch.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 15 Jun 2013
   FreeBSD-style copyright and disclaimer apply

   Platform specific information.
*/

#ifndef __lockless__arch_h__
#define __lockless__arch_h__

#include <cstddef>

/******************************************************************************/
/* CONSTANTS                                                                  */
/******************************************************************************/

namespace lockless {

#define locklessCacheLine 64
static constexpr size_t CacheLine = locklessCacheLine;

#define locklessPageSize 4096
static constexpr size_t PageSize  = locklessPageSize;

} // lockless


/******************************************************************************/
/* COMPILERS                                                                  */
/******************************************************************************/

#define locklessAlign(x)     __attribute__((aligned(x)))
#define locklessPacked       __attribute__((__packed__))
#define locklessAlwaysInline __attribute__((always_inline))
#define locklessNeverInline  __attribute__((noinline))
#define locklessNonNull      __attribute__((nonnull))
#define locklessNoReturn     __attribute__((noreturn))
#define locklessPure         __attribute__((pure))
#define locklessPrintf(x,y)  __attribute__((format(printf, x, y)))
#define locklessMalloc       __attribute__((malloc))
#define locklessLikely(x)    __builtin_expect(x,true)
#define locklessUnlikely(x)  __builtin_expect(x,false)


/******************************************************************************/
/* UTILS                                                                      */
/******************************************************************************/

#define locklessCacheAligned locklessAlign(locklessCacheLine)


#endif // __lockless__arch_h__
