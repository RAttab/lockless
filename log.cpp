/* log.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 30 Dec 2012
   FreeBSD-style copyright and disclaimer apply

   Description

*/

#include "log.h"

namespace lockless {

namespace details {

Clock<size_t> GlobalLogClock;

} // namespace details

Log<1024> GlobalLog;

} // lockless
