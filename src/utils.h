/** utils.h                                 -*- C++ -*-
    Rémi Attab (remi.attab@gmail.com), 19 Dec 2012
    FreeBSD-style copyright and disclaimer apply

    Various utilities.

*/

#ifndef __lockless__utils_h__
#define __lockless__utils_h__

#include <array>
#include <atomic>
#include <string>
#include <cstdlib>
#include <stdio.h>

namespace lockless {


/******************************************************************************/
/* STATIC COMP                                                                */
/******************************************************************************/

/** \todo Still undecided about whether it's a good idea to have this... */
#define locklessEnum static constexpr

#define locklessStaticAssert(x) \
    static_assert(x, #x)

template<size_t X>
struct IsPow2
{
    locklessEnum bool value = (X & (X - 1ULL)) == 0ULL;
};



/******************************************************************************/
/* CEIL DIV                                                                   */
/******************************************************************************/

/** I'm aware that this doesn't take into account the bajillion of corner cases
    with integer division. That being said, it's only to be used for unsigned
    integers where underflows are taken care of. I don't believe it's possible
    to overflow it but it probably is so don't do that...
*/
template<size_t Num, size_t Div>
struct CeilDiv
{
    locklessEnum size_t value = (Num - 1) / Div + 1;
};

template<size_t Div> struct CeilDiv<  0, Div> { locklessEnum size_t value = 0; };
template<size_t Num> struct CeilDiv<Num,   0> { locklessEnum size_t value = 0; };
template<>           struct CeilDiv<  0,   0> { locklessEnum size_t value = 0; };


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
/* PADDING                                                                    */
/******************************************************************************/

/** Work-around for two problems:
    - C++ doesn't like zero sized arrays.
    - sizeof(struct{}) == 1

    Since I don't know of any other way to get a zero sized object in C++ then I
    have no choice but to asume that there's always going to be at least 1 byte
    dedicated to padding regardless of whether it's needed or not.

    For the record, I hate this solution and I hate whoever decided that
    sizeof(struct{}) should be 1.
 */
template<size_t Size, size_t Align>
struct Padding
{
    locklessStaticAssert(IsPow2<Align>::value);

    locklessEnum size_t Leftover = Size % Align;
    locklessEnum size_t PadBytes =
        Align == 1 ? 1 : (Leftover ? Align - Leftover : Align);

    uint8_t padding[PadBytes];
};


/******************************************************************************/
/* UNWRAP                                                                     */
/******************************************************************************/

template<typename T>
struct Unwrap
{
    typedef T Ret;
    static Ret get(const T& val) { return val; }
};

// Allows printout of std::atomics without having to load them out first.
template<typename T>
struct Unwrap< std::atomic<T> >
{
    typedef T Ret;
    static Ret get(const std::atomic<T>& val) { return val; }
};

template<typename T>
typename Unwrap<T>::Ret unwrap(const T& val) { return Unwrap<T>::get(val); }


/******************************************************************************/
/* FORMAT                                                                     */
/******************************************************************************/
// \todo GCC has a printf param check builtin. No idea if it works with variadic
// templates.

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
            buffer.data(), buffer.size(), pattern, unwrap(args)...);

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

