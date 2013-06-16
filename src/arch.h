/* arch.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 15 Jun 2013
   FreeBSD-style copyright and disclaimer apply

   Platform specific information.
*/

#ifndef __lockless__arch_h__
#define __lockless__arch_h__

namespace lockless {


/******************************************************************************/
/* CONSTANTS                                                                  */
/******************************************************************************/

// \todo Configure at build time.
enum {
    CacheLine = 64,
    PageSize  = 4096,
};


} // lockless

#endif // __lockless__arch_h__
