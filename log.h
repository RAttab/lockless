/** log.h                                 -*- C++ -*-
    Rémi Attab (remi.attab@gmail.com), 24 Dec 2012
    FreeBSD-style copyright and disclaimer apply

    Lockfree logger used for debugging with minimal lantency.

*/

#ifndef __lockless__log_h__
#define __lockless__log_h__

#include "utils.h"

#include <array>
#include <string>
#include <iostream>

namespace lockless {

/******************************************************************************/
/* LOG TYPE                                                                   */
/******************************************************************************/

enum LogType
{
    LogRcu   = 0x100,
    LogQueue = 0x200,
    LogMap   = 0x300,
};

inline std::string to_string(LogType type)
{
    // Try to to keep the same number of char for each.
    switch (type) {

    case LogRcu:   return "Rcu  ";
    case LogQueue: return "Queue";
    case LogMap:   return "Map  ";
    default:       return "-----";

    }
}


/******************************************************************************/
/* LOG ENTRY                                                                  */
/******************************************************************************/

struct LogEntry
{
    template<typename Str>
    LogEntry(LogType type, double ts, Str&& log) :
        type(type), ts(ts), log(std::forward<Str>(log))
    {}

    std::string print(double tsOffset = 0.0) const
    {
        std::array<char, 256> buffer;

        int written = snprinf(
                buffer.data(), buffer.size(),
                "<%10.6lf>[%s]: %s",
                (ts - tsOffset), to_string(type), log.c_str());

        if (writen < 0) return "LOG ERROR";
        if (written > buffer.size()) written = buffer.size();

        return std::string(buffer.data(), written);
    }

    LogType type;
    double ts;
    std::string log;
};


/******************************************************************************/
/* LOGGER                                                                     */
/******************************************************************************/

template<size_t Size>
struct Log
{
    /* Blah

     */
    Log(bool sampleTime = true) :
        sampleTime(sampleTime), index(0)
    {
        for (auto& entry : log) entry.store(nullptr);
    }

    /* Blah

     */
    template<typename LogFirst, typename LogSecond>
    Log(    const LogFirst& first,
            const LogSecond& second,
            double sampleTime = true) :
        sampleTime(sampleTime), index(0)
    {
        auto firstDump = first.dump();
        auto secondDump = second.dump();

        size_t i = 0;
        size_t j = 0;

        while (i + j < firstDump.size() + secondDump.size()) {

            LogEntry* entry;

            if (i == firstDump.size())
                entry = &secondDump[j++];

            else if (j == secondDump.size())
                entry = &firstDump[i++];

            else if (firstDump[i].ts <= secondDump[j].ts)
                entry = &firstDump[i++];

            else entry = &secondDump[j++];

            log(entry->type, entry->log, entry->ts);
        }
    }

    Log(const Log& other) = delete;
    Log& operator=(const Log& other) = delete;

    size_t size() const { return Size; }

    /* Blah

     */
    template<typename Str>
        void log(LogType type, Str&& log, double ts = -1)
    {
        if (sampleTime && ts < 0) ts = Time::wall();

        size_t i = index.fetch_add(1) % log.size();
        LogEntry* old = log[i].exchange(new LogEntry(type, ts, log));

        if (old) delete old;
    }

    /* Blah

     */
    std::vector<LogEntry> dump()
    {
        std::vector<LogEntry> dump;
        dump.reserve(log.size());

        size_t start = index.load();
        for (size_t i = start; i != start; i = (i + 1) % log.size()) {
            LogEntry* entry = log[i].exchange(nullptr);
            dump.push_pack(*entry);
            delete entry;
        }

        return dump;
    }

private:
    bool sampleTime;
    std::atomic<size_t> index;
    std::array<std::atomic<LogEntry*>, Size> log;
};


/******************************************************************************/
/* LOG SINK                                                                   */
/******************************************************************************/

/* Blah

 */
void logToStream(const Log& log, const std::ostream& stream = std::cerr)
{
    std::vector<LogEntry> entries = log.dump();

    for (const LogEntry& entry : entries)
        stream << entry.print() << "\n";

    stream.flush();
}

} // lockless

#endif // __lockless__log_h__
