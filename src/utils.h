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


/******************************************************************************/
/* FORMAT                                                                     */
/******************************************************************************/
// \todo GCC has a printf param check builtin. No idea if it works with variadic
// templates.

namespace details {

template<typename T>
struct Unwrap
{
    static T get(const T& val) { return val; }
};

// Allows printout of std::atomics without having to load them out first.
template<typename T>
struct Unwrap< std::atomic<T> >
{
    static T get(const std::atomic<T>& val) { return val.load(); }
};

} // namespace details


template<typename... Args>
std::string format(const std::string& pattern, const Args&... args)
{
    return format(pattern.c_str(), args...);
}

template<typename... Args>
std::string format(const char* pattern, const Args&... args)
{
    std::array<char, 256> buffer;

    size_t chars = snprintf(
            buffer.data(), buffer.size(), pattern,
            details::Unwrap<Args>::get(args)...);

    return std::string(buffer.data(), chars);
}


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

template<typename T>
std::string to_string(const T* p)
{
    return std::to_string(reinterpret_cast<const void*>(p));
}

template<typename First, typename Second>
std::string to_string(const std::pair<First, Second>& p)
{
    return format("<%s, %s>",
            to_string(p.first).c_str(),
            to_string(p.second).c_str());
}

} // namespace std


#endif // __lockless__utils_h__

