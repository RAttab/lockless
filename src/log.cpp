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
Log<1024> GlobalLog;


/******************************************************************************/
/* LOG TYPE                                                                   */
/******************************************************************************/

string to_string(LogType type)
{
    // Try to to keep the same number of char for each.
    switch (type) {
    case LogRcu:   return "Rcu  ";
    case LogQueue: return "Queue";
    case LogMap:   return "Map  ";
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
    array<char, 256> buffer;

    int written = snprintf(
            buffer.data(), buffer.size(),
            "%6ld < %s > %-10s: %s",
            tick,
            to_string(type).c_str(),
            title.c_str(),
            msg.c_str());

    if (written < 0) return "LOG ERROR";
    written = min<unsigned>(written, buffer.size());

    return string(buffer.data(), written);
}

} // lockless
