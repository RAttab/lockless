/** clock.h                                 -*- C++ -*-
    Rémi Attab (remi.attab@gmail.com), 25 Dec 2012
    FreeBSD-style copyright and disclaimer apply

    Returns a unique tick each time it's called.

*/

#ifndef __lockless__clock_h__
#define __lockless__clock_h__

#include "utils.h"

#include <atomic>
#include <type_traits>

namespace lockless {


/******************************************************************************/
/* CLOCK                                                                      */
/******************************************************************************/

template<typename T>
struct Clock
{
    locklessStaticAssert(std::is_integral<T>::value);

    Clock() : ticks(0) {}

    T tick() { return ticks.fetch_add(1); }
    T now() const { return ticks.load(); }

    /* Compairaison that is safe even in the case of overflows. Assumes that a 2
       timestamps will be compared to each other within a reasonable number of
       ticks.
     */
    static int compare(T lhs, T rhs)
    {
        if (lhs == rhs) return 0;

        // Assume we have enough bits to not ever overflow.
        if (sizeof(T) == 8) return lhs < rhs ? -1 : 1;

        const T mask = static_cast<T>(3) << (sizeof(T) * 8 - 2);
        const T lhsMsb = lhs & mask;
        const T rhsMsb = rhs & mask;

        if (lhsMsb == rhsMsb) return lhs < rhs ? -1 : 1;
        if (lhsMsb == 0) return rhsMsb == mask ? 1 : -1;
        if (rhsMsb == 0) return lhsMsb == mask ? -1 : 1;
        return lhsMsb < rhsMsb ? -1 : 1;
    }

private:
    std::atomic<T> ticks;
};

} // lockless

#endif // __lockless__clock_h__
