/* arch.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 15 Jun 2013
   FreeBSD-style copyright and disclaimer apply

   Platform specific information.
*/

#ifndef __lockless__arch_h__
#define __lockless__arch_h__

#include <cstddef>


/******************************************************************************/
/* COMPILERS                                                                  */
/******************************************************************************/

#define locklessAlign(x) __attribute__((aligned(x)))
#define locklessPacked __attribute__((__packed__))


/******************************************************************************/
/* CONSTANTS                                                                  */
/******************************************************************************/

namespace lockless {

// \todo Configure at build time.
static constexpr size_t CacheLine = 64;
static constexpr size_t PageSize  = 4096;

} // lockless

#endif // __lockless__arch_h__
