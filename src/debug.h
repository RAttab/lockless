/* debug.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 31 Dec 2012
   FreeBSD-style copyright and disclaimer apply

   Macros to easily enable/disable debug code.
*/

#ifndef __lockless__debug_h__
#define __lockless__debug_h__

namespace lockless {


/******************************************************************************/
/* DEBUG FLAGS                                                                */
/******************************************************************************/

#ifndef LOCKLESS_LOG_DEBUG
#   define LOCKLESS_LOG_DEBUG 0
#endif

#ifndef LOCKLESS_RCU_DEBUG
#   define LOCKLESS_RCU_DEBUG 0
#endif

#ifndef LOCKLESS_LIST_DEBUG
#   define LOCKLESS_LIST_DEBUG 0
#endif

#ifndef LOCKLESS_QUEUE_DEBUG
#   define LOCKLESS_QUEUE_DEBUG 0
#endif

#ifndef LOCKLESS_MAP_DEBUG
#   define LOCKLESS_MAP_DEBUG 0
#endif

#ifndef LOCKLESS_SNZI_DEBUG
#   define LOCKLESS_SNZI_DEBUG 0
#endif

#ifndef LOCKLESS_ALLOC_DEBUG
#   define LOCKLESS_ALLOC_DEBUG 0
#endif

#ifndef LOCKLESS_CHECK_ABORT
#   define LOCKLESS_CHECK_ABORT 0
#endif

enum DebugFlags
{
    DebugLog   = LOCKLESS_LOG_DEBUG,
    DebugRcu   = LOCKLESS_RCU_DEBUG,
    DebugQueue = LOCKLESS_QUEUE_DEBUG,
    DebugList  = LOCKLESS_LIST_DEBUG,
    DebugMap   = LOCKLESS_MAP_DEBUG,
    DebugSnzi  = LOCKLESS_SNZI_DEBUG,
    DebugAlloc = LOCKLESS_ALLOC_DEBUG,

    CheckAbort = LOCKLESS_CHECK_ABORT,
};

} // lockless

#endif // __lockless__debug_h__
