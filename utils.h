/** utils.h                                 -*- C++ -*-
    Rémi Attab (remi.attab@gmail.com), 19 Dec 2012
    FreeBSD-style copyright and disclaimer apply

    Various utilities.

*/

#ifndef __lockless__utils_h__
#define __lockless__utils_h__

#include <time.h>
#include <cstdlib>

namespace lockless {


/******************************************************************************/
/* MALLOC DELETER                                                             */
/******************************************************************************/

struct MallocDeleter
{
    void operator() (void* ptr)
    {
        std::free(ptr);
    }
};


/******************************************************************************/
/* TIME                                                                       */
/******************************************************************************/

struct Time
{
    static double wall()
    {
        struct timespec ts;
        if (clock_gettime(CLOCK_REALIME, &ts) < 0)
            return -1;

        return ts.tv_sec + (tv_nsec * 0.0000000001);
    }
};

} // lockless

#endif // __lockless__utils_h__
