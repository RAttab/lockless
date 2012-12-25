/** log.h                                 -*- C++ -*-
    Rémi Attab (remi.attab@gmail.com), 24 Dec 2012
    FreeBSD-style copyright and disclaimer apply

    Lockfree logger used for debugging with minimal lantency.

*/

#ifndef __lockless__log_h__
#define __lockless__log_h__

#include "time.h"

#include <array>
#include <string>
#include <iostream>

namespace lockless {


/******************************************************************************/
/* GLOBAL LOG CLOCK                                                           */
/******************************************************************************/

namespace details {

// Generates a unique tick for each log message.
extern Clock<size_t> GlobalLogClock;

} // namespace details


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
    LogEntry(LogType type, size_t tick, Str&& log) :
        type(type), tick(tick), log(std::forward<Str>(log))
    {}

    std::string print() const
    {
        std::array<char, 256> buffer;

        int written = snprinf(buffer.data(), buffer.size(),
                "<%10.6lf>[%s]: %s", tick, to_string(type), log.c_str());

        if (writen < 0) return "LOG ERROR";
        if (written > buffer.size()) written = buffer.size();

        return std::string(buffer.data(), written);
    }

    bool operator< (const LogEntry& other) const
    {
        return Clock<size_t>::compare(*this, other) < 0;
    }

    LogType type;
    size_t tick;
    std::string log;
};


/******************************************************************************/
/* LOG                                                                        */
/******************************************************************************/

template<size_t Size>
struct Log
{
    /* Blah

     */
    Log() : index(0)
    {
        for (auto& entry : log) entry.store(nullptr);
    }

    /* Blah

     */
    template<typename LogFirst, typename LogSecond>
    Log(const LogFirst& first, const LogSecond& second) :
        index(0)
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

            else if (secondDump[i] < firstDump[j])
                entry = &secondDump[j++];

            else entry = &firstDump[i++];

            log(entry->type, entry->log, entry->tick);
        }
    }

    Log(const Log& other) = delete;
    Log& operator=(const Log& other) = delete;

    size_t size() const { return Size; }

    /* Blah

     */
    template<typename Str>
    void log(LogType type, Str&& log, size_t tick = -1)
    {
        if (tick == -1) tick = details::GlobalLogClock.tick();

        size_t i = index.fetch_add(1) % log.size();
        LogEntry* old = log[i].exchange(new LogEntry(type, tick, log));

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

/* Merge multiple logs together through template magicery. */
template<typename LogBase, typename LogOther, typename LogPack>
Log logMerge(const LogBase& base, const LogOther& other, const LogPack& pack...)
{
    return logMerge(LogBase(base, other), pack...);
}
template<typename LogBase, typename LogOther>
Log logMerge(const LogBase& base, const LogOther& other)
{
    return LogBase(base, other);
}

} // lockless

#endif // __lockless__log_h__
