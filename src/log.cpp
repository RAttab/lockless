/* log.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 30 Dec 2012
   FreeBSD-style copyright and disclaimer apply

   Description

*/

#include "log.h"

using namespace std;

namespace lockless {


/******************************************************************************/
/* GLOBAL VAR                                                                 */
/******************************************************************************/

namespace details { Clock<size_t> GlobalLogClock; }


/******************************************************************************/
/* LOG TYPE                                                                   */
/******************************************************************************/

string to_string(LogType type)
{
    // Try to to keep the same number of char for each.
    switch (type) {
    case LogMisc:  return " Msc ";
    case LogRcu:   return " Rcu ";
    case LogQueue: return "Queue";
    case LogMap:   return " Map ";
    case LogAlloc: return "Alloc";
    default:       return "-----";
    }
}


/******************************************************************************/
/* LOG ENTRY                                                                  */
/******************************************************************************/

string
LogEntry::
print() const
{
    return format("%8ld {%2ld} <%s> %-10s: %s",
            tick, threadId, to_string(type).c_str(), title.c_str(), msg.c_str());
}

} // lockless
