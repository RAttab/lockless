/* arch.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 15 Jun 2013
   FreeBSD-style copyright and disclaimer apply

   Platform specific information.
*/

#ifndef __lockless__arch_h__
#define __lockless__arch_h__

#include "utils.h"

namespace lockless {

/******************************************************************************/
/* CONSTANTS                                                                  */
/******************************************************************************/

// \todo Configure at build time.
locklessEnum size_t CacheLine = 64;
locklessEnum size_t PageSize  = 4096;


} // lockless

#endif // __lockless__arch_h__
