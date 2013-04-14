/** utils.h                                 -*- C++ -*-
    Rémi Attab (remi.attab@gmail.com), 19 Dec 2012
    FreeBSD-style copyright and disclaimer apply

    Various utilities.

*/

#ifndef __lockless__utils_h__
#define __lockless__utils_h__

#include <cstdlib>
#include <string>
#include <array>
#include <stdio.h>

namespace lockless {


/******************************************************************************/
/* ARCH                                                                       */
/******************************************************************************/

// \todo Configure at build time.
enum { CacheLine = 64 };


/******************************************************************************/
/* MALLOC DELETER                                                             */
/******************************************************************************/

struct MallocDeleter
{
    void operator() (void* ptr)
    {
        free(ptr);
    }
};

} // lockless


/******************************************************************************/
/* TO STRING                                                                  */
/******************************************************************************/

namespace std {

inline std::string to_string(const std::string& str) { return str; }

template<typename T>
std::string to_string(T* p)
{
    return std::to_string(reinterpret_cast<void*>(p));
}

template<typename First, typename Second>
std::string to_string(const std::pair<First, Second>& p)
{
    std::array<char, 80> buffer;
    snprintf(buffer.data(), buffer.size(), "<%s, %s>",
            to_string(p.first).c_str(), to_string(p.second).c_str());
    return std::string(buffer.data());
}

} // namespace std


#endif // __lockless__utils_h__

