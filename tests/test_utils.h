/** test.h                                 -*- C++ -*-
    Rémi Attab (remi.attab@gmail.com), 24 Dec 2012
    FreeBSD-style copyright and disclaimer apply

    Testing framework for multithreaded algorithms

*/

#ifndef __lockess__test_h__
#define __lockess__test_h__

#include <time.h>
#include <unistd.h>
#include <set>
#include <cstdlib>
#include <cmath>

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


/******************************************************************************/
/* TIME DIST                                                                  */
/******************************************************************************/

struct TimeDist
{
    void add(double ts) { dist.insert(ts); }

    double min() const { return *dist.begin(); }
    double max() const { return *dist.rbegin(); }

    double median() const
    {
        auto it = dist.begin();
        std::advance(it, dist.size() / 2);
        return *it;
    }

    double avg() const
    {
        double sum = std::accumulate(dist.begin(), dist.end(), 0.0);
        return sum / dist.size();
    }

    double variance() const
    {
        double u = avg();

        auto diffSqrt = [=] (double total, double x) {
            return total + std::pow(u - x, 2);
        };

        double sum = std::accumulate(dist.begin(), dist.end(), 0.0, diffSqrt);
        return sum / dist.size();
    }

    double stderr() const
    {
        return std::sqrt(variance());
    }

    TimeDist& operator+= (const TimeDist& other)
    {
        dist.insert(other.dist.begin(), other.dist.end());
        return *this;
    }

private:

    std::multiset<double> dist;

};


/******************************************************************************/
/* FORMAT                                                                     */
/******************************************************************************/

std::string fmtElapsed(double elapsed)
{
    char scale;

    if (elapsed >= 1.0) scale = 's';

    if (elapsed < 1.0) {
        elapsed *= 1000.0;
        scale = 'm';
    }

    if (elapsed < 1.0) {
        elapsed *= 1000.0;
        scale = 'u';
    }

    if (elapsed < 1.0) {
        elapsed *= 1000.0;
        scale = 'n';
    }

    std::array<char, 32> buffer;
    snprintf(buffer.data(), buffer.size(), "%6.2f%c", elapsed, scale);
    return std::string(buffer.data());
}


std::string fmtValue(double value)
{
    char scale;

    if (value >= 1.0) scale = ' ';

    if (value >= 1000.0) {
        value /= 1000.0;
        scale = 'k';
    }

    if (value >= 1000.0) {
        value /= 1000.0;
        scale = 'm';
    }

    if (value >= 1000.0) {
        value /= 1000.0;
        scale = 'g';
    }

    std::array<char, 32> buffer;
    snprintf(buffer.data(), buffer.size(), "%6.2f%c", value, scale);
    return std::string(buffer.data());
}


} // lockless

#endif // __lockess__test_h__
