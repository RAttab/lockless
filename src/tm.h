/* time.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 16 May 2013
   FreeBSD-style copyright and disclaimer apply

   Time related utilities.
*/

#ifndef __lockless__time_h__
#define __lockless__time_h__

#include <cstdlib>
#include <time.h>
#include <unistd.h>

namespace lockless {

/******************************************************************************/
/* TIME                                                                       */
/******************************************************************************/

struct Time
{
    static double wall()
    {
        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
            return -1;

        return ts.tv_sec + (ts.tv_nsec * 0.0000000001);
    }
};


/******************************************************************************/
/* TIMER                                                                      */
/******************************************************************************/

struct Timer
{
    Timer() : start(Time::wall()) {}

    double elapsed() const
    {
        double end = Time::wall();
        double adj = Time::wall();
        return (end - start) - (adj - end);
    }

private:
    double start;
};


} // lockless

#endif // __lockless__time_h__
