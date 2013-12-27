/** utils.h                                 -*- C++ -*-
    Rémi Attab (remi.attab@gmail.com), 19 Dec 2012
    FreeBSD-style copyright and disclaimer apply

    Various utilities.

*/

#ifndef __lockless__utils_h__
#define __lockless__utils_h__

#include "format.h"

#include <array>
#include <string>
#include <cstdlib>

namespace lockless {


/******************************************************************************/
/* STATIC COMP                                                                */
/******************************************************************************/

/** \todo Still undecided about whether it's a good idea to have this... */
#define locklessEnum static constexpr

#define locklessStaticAssert(x) static_assert(x, #x)

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
    to overflow it but it probably is so... don't do that.
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
/** Turns out that it's ridiculously difficult to manually pad a struct in C++
    even when we allow for GCC extensions. Here's a fun example to demonstrate:

        struct V { std::atomic<uint64_t> v; };

        struct P { uint8_t p; };
        struct C : public V, public P  {};

    Before you mention the ridiculous setup, know that sizeof(struct {}) in C++
    is equal to 1 (principle of least astonishment...? what's that?) so instead
    we have to rely on some obscure optimization rule in C++ where if a class
    derives from an empty struct then the compiler is allowed to overlap the
    layout of the empty struct over the other struct.

    The idea is that we want sizeof(C) == 9 and there seems to be a grand total
    of 2 gcc specific ways of doing this:

    The first is to use __attribute__((packed)) on V but that doesn't work
    because std::atomic isn't a POD. Oh and no, using it on P and C doesn't do
    do anything. Why? Who the fuck knows...

    The second is to use the pack pragma which is compatibility layer for MVCC
    which will, I kid you not, work if we surround the P and C declaration with
    it. Why does this work but not the packed attribute? Who the fuck knows...

    Now you'd think we'd use the pragma and be home free right? Wrong. How the
    hell do pragma even work when we're dealing with templates? Where are the
    pragma even supposed to go? Around the declaration of the template?
    Nop. Around the declaration of the variable which instantiates the template?
    Nop. Around both maybe? Nop. Well, that's another useless solution...

    Looks like we're well and truly fucked then. Well, not quite. Remember how I
    mentioned that packing V wouldn't work because std::atomic is not a pod?
    Well, this is the one use case for AtomicPod because it's a light clone of
    std::atomic that also happens to have a copy and move constructor.

    Here's a summary of the packing rules for GCC:

        struct Short { short a }; // sizeof 2
        Pad<Short, 3> PadShort;   // sizeof 4

        struct Int { int a }; // sizeof 4
        Pad<Int, 3> PadInt;   // sizeof 8

        struct Big { uint8_t[17] a; }; // sizeof 24
        Pad<Big, 5> PadBig;            // sizeof 32

    So gcc always rounds the padding to the size of the struct for values less
    then 8. Anything above will be rounded to 8 as well. Note that this was
    tested experimentally so I may be getting this horribly wrong.

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


/** To summarize the giant wall of text above, T must be packed and therefor
    POD-ed for this class to have any chance of doing anything when working for
    some edge cases. Since the rules are annoyingly tricky to get right, there's
    a CheckPad struct which allows you to determine whether a struct needs to be
    packed or not.
 */
template<typename T, size_t Align>
struct Pad :
        public T,
        private details::Padding<details::CalcPadding<T, Align>::value>
{};


/** Since the packing rules are so damn hard to figure out, this class allows
    you to quickly check whether we need to pack T or not. Creating a static
    assert after every instanciation of Pad is highly recommended.
 */
template<typename T, size_t Align>
struct CheckPad
{
    // If this is false then T needs to be packed.
    locklessEnum bool value = (sizeof(Pad<T,Align>) % Align) == 0ULL;
};

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
    return lockless::toString(begin(a), end(a));
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

