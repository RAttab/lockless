/* format.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 27 Dec 2013
   FreeBSD-style copyright and disclaimer apply

   String formatting utilities
*/

#pragma once

#include <array>
#include <atomic>
#include <string>
#include <stdio.h>

namespace lockless {


/******************************************************************************/
/* UNWRAP                                                                     */
/******************************************************************************/

namespace details {


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


} // namespace details


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
            buffer.data(), buffer.size(), pattern, details::unwrap(args)...);

    return std::string(buffer.data(), chars);
}


/******************************************************************************/
/* FORMAT UTILS                                                               */
/******************************************************************************/

inline std::string fmtElapsedSmall(double elapsed)
{
    static const std::string scaleIndicators = "smunpf?";

    size_t i = 0;
    while (elapsed < 1.0 && i < (scaleIndicators.size() - 1)) {
        elapsed *= 1000.0;
        i++;
    }

    return lockless::format("%6.2f%c", elapsed, scaleIndicators[i]);
}

inline std::string fmtElapsedLarge(double elapsed)
{
    char indicator = 's';

    if (elapsed >= 60.0) {
        elapsed /= 60.0;
        indicator = 'M';
    }
    else goto done;

    if (elapsed >= 60.0) {
        elapsed /= 60.0;
        indicator = 'H';
    }
    else goto done;

    if (elapsed >= 24.0) {
        elapsed /= 24.0;
        indicator = 'D';
    }
    else goto done;

  done:
    return lockless::format("%6.2f%c", elapsed, indicator);
}

inline std::string fmtElapsed(double elapsed)
{
    if (elapsed < 60.0)
        return fmtElapsedSmall(elapsed);
    return fmtElapsedLarge(elapsed);
}

inline std::string fmtValue(double value)
{
    static const std::string scaleIndicators = " kmgth?";

    size_t i = 0;
    while (value >= 1000.0 && i < (scaleIndicators.size() - 1)) {
        value /= 1000.0;
        i++;
    }

    return format("%6.2f%c", value, scaleIndicators[i]);
}

inline std::string fmtRatio(double num, double denom)
{
    double value = (num / denom) * 100;
    return format("%6.2f%%", value);
}


inline std::string fmtTitle(const std::string& title, char fill = '-')
{
    std::string filler(80 - title.size() - 4, fill);
    return format("[ %s ]%s", title.c_str(), filler.c_str());
}

} // lockless
