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
/** My conclusion of the day after having experimented with manually padding in
    C++:

        C++ and g++ are both steaming piles of shit.

    Here's why:

        struct V { std::atomic<uint64_t> v; };

        struct P { uint8_t p; };
        struct C : public V, public P  {};

    Before you mention the ridiculous setup, know that sizeof(struct {}) in C++
    is equal to 1 (principle of least astonishment...? what's that?) so instead
    we have to rely on some obscure optimization rule in C++ where if a class
    derives from an empty struct then the compiler is allowed to overlap the
    layout of the empty struct over the other struct.

    The idea is that we want sizeof(C) == 9 and there seems to be a grand total
    of 2 ways (gcc specific cause fuck portability) to do this:

    The first is to use __attribute__((packed)) on V but that doesn't work
    because std::atomic isn't a POD. Oh and no, using it on P and C doesn't do
    fuck-all. Why? Who the fuck knows...

    The second is to use the pack pragma which is compatibility layer for MVCC
    which will, I kid you not, work if we surround the P and C declaration with
    it. Why does this work and the packed attribute not? Who the fuck knows...

    Now you'd think we'd use the pragma and be home free right? Wrong. Good luck
    trying to make that work with templates... Where are the pragma even
    supposed to go? Around the declaration of the template? Nop. Around the
    declaration of the variable which instantiates the template? Nop. Around
    both maybe? Nop. Well, that's another useless solution...

    Oh and the cherry on the sundea is that the doc for both the attribute and
    the pragma is completely useless and mentions none of these marvelous
    pitfalls. Welcome to g++, where adding a single byte to a struct is fucking
    impossible. Time to learn assembly me-think.
 */

namespace details {

template<size_t Size> struct Padding    { uint8_t padding[Size]; };
template<>            struct Padding<0> {};

template<typename T, size_t Align>
struct CalcPadding
{
    locklessEnum size_t leftover = sizeof(T) % Align;
    locklessEnum size_t value = leftover ? Align - leftover : 0;
};

} // namespace details

template<typename T, size_t Align>
struct Pad :
        public T,
        private details::Padding<details::CalcPadding<T, Align>::value>
{};


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
    std::array<char, 1024> buffer;

    size_t chars = snprintf(
            buffer.data(), buffer.size(), pattern, unwrap(args)...);

    return std::string(buffer.data(), chars);
}



/******************************************************************************/
/* TO STRING                                                                  */
/******************************************************************************/

template<typename Iterator>
std::string toString(Iterator it, Iterator last);

template<typename Cont>
std::string toStringCont(const Cont& c)
{
    return toString(begin(c), end(c));
}

} // lockless


namespace std {

inline std::string to_string(const std::string& str) { return str; }

template<typename T>
std::string to_string(T* p)
{
    return lockless::format("%p", p);
}

template<typename T>
std::string to_string(const T* p)
{
    return lockless::format("%p", p);
}

template<typename First, typename Second>
std::string to_string(const std::pair<First, Second>& p)
{
    return format("<%s, %s>",
            to_string(p.first).c_str(),
            to_string(p.second).c_str());
}


template<typename T, size_t N> struct array;
template<typename T, size_t N>
std::string to_string(const std::array<T, N>& a)
{
    return lockless::toString(begin(a), begin(a));
}

} // namespace std

namespace lockless {

// Needs to reside down here so the to_string(std::pair) overload is visible.
template<typename Iterator>
std::string toString(Iterator it, Iterator last)
{
    std::string str = "[ ";
    for (; it != last; ++it)
        str += std::to_string(*it) + " ";
    str += "]";
    return str;
}

} // namespace lockless



#endif // __lockless__utils_h__

