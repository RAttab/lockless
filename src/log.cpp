/* log.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 30 Dec 2012
   FreeBSD-style copyright and disclaimer apply

   Description

*/

#include "log.h"
#include "tls.h"

using namespace std;

namespace lockless {


/******************************************************************************/
/* GLOBAL VAR                                                                 */
/******************************************************************************/

namespace details { Clock<size_t> GlobalLogClock; }


/******************************************************************************/
/* THREAD ID                                                                  */
/******************************************************************************/

namespace details {

namespace {

atomic<size_t> GlobalThreadCounter{1};

LOCKLESS_TLS size_t LocalThreadId{0};

} // namespace anonymous


size_t threadId()
{
    if (!LocalThreadId)
        LocalThreadId = GlobalThreadCounter.fetch_add(1);
    return LocalThreadId;
}

} // namespace details


/******************************************************************************/
/* LOG TYPE                                                                   */
/******************************************************************************/

string to_string(LogType type)
{
    // Try to to keep the same number of char for each.
    switch (type) {
    case LogMisc:  return "Misc ";
    case LogRcu:   return " Rcu ";
    case LogQueue: return "Queue";
    case LogMap:   return " Map ";
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
