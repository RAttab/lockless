/* log.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 24 Dec 2012
   FreeBSD-style copyright and disclaimer apply

   Lockfree logger used for debugging with minimal lantency.
 */

#ifndef __lockless__log_h__
#define __lockless__log_h__

#include "clock.h"

#include <stdio.h>
#include <array>
#include <vector>
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
    template<typename Title, typename Msg>
    LogEntry(LogType type, size_t tick, Title&& title, Msg&& msg) :
        type(type),
        tick(tick),
        title(std::forward<Title>(title)),
        msg(std::forward<Msg>(msg))
    {}

    std::string print() const
    {
        std::array<char, 256> buffer;

        int written = snprintf(
                buffer.data(), buffer.size(),
                "%6ld [%s] %-10s: %s",
                tick,
                to_string(type).c_str(),
                title.c_str(),
                msg.c_str());

        if (written < 0) return "LOG ERROR";
        written = std::min<unsigned>(written, buffer.size());

        return std::string(buffer.data(), written);
    }

    bool operator< (const LogEntry& other) const
    {
        return Clock<size_t>::compare(this->tick, other.tick) < 0;
    }

    LogType type;
    size_t tick;
    std::string title;
    std::string msg;
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
        for (auto& entry : logs) entry.store(nullptr);
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

            log(entry->type, entry->title, entry->msg, entry->tick);
        }
    }

    Log(const Log& other) = delete;
    Log& operator=(const Log& other) = delete;

    size_t size() const { return Size; }

    /* Blah

     */
    template<typename Title, typename Msg>
    void log(LogType type, size_t tick, Title&& title, Msg&& msg)
    {
        size_t i = index.fetch_add(1) % logs.size();
        LogEntry* old = logs[i].exchange(new LogEntry(type, tick, title, msg));

        if (old) delete old;
    }

    /* Blah

     */
    template<typename Title, typename Msg>
    void log(LogType type, Title&& title, Msg&& msg)
    {
        log(type, details::GlobalLogClock.tick(), title, msg);
    }

    /* Blah

       \todo GCC has a printf param check. No idea if it works with variadic
             templates.
     */
    template<typename Title, typename... Args>
    void log(
            LogType type,
            Title&& title,
            const std::string& format,
            Args&&... args)
    {
        std::array<char, 256> buffer;
        snprintf(buffer.data(), buffer.size(), format.c_str(), args...);
        log(type, title, std::string(buffer.data()));
    }

    /* Blah

     */
    std::vector<LogEntry> dump()
    {
        std::vector<LogEntry> dump;
        dump.reserve(logs.size());

        size_t end = index.load() % logs.size();
        size_t start = (end + 1) % logs.size();

        for (size_t i = start; i != end; i = (i + 1) % logs.size()) {
            LogEntry* entry = logs[i].exchange(nullptr);
            if (!entry) continue;

            dump.push_back(*entry);
            delete entry;
        }

        return dump;
    }

private:

    std::atomic<size_t> index;
    std::array<std::atomic<LogEntry*>, Size> logs;

};


/******************************************************************************/
/* LOG SINK                                                                   */
/******************************************************************************/

/* Blah

 */
template<typename Log>
void logToStream(Log& log, std::ostream& stream = std::cerr)
{
    std::vector<LogEntry> entries = log.dump();

    for (const LogEntry& entry : entries)
        stream << entry.print() << "\n";

    stream.flush();
}

/* Merge multiple logs together through template magicery. */
template<typename LogBase, typename LogOther, typename... LogPack>
LogBase logMerge(LogBase& base, LogOther& other, LogPack&... pack)
{
    return logMerge(LogBase(base, other), pack...);
}
template<typename LogBase, typename LogOther>
LogBase logMerge(const LogBase& base, const LogOther& other)
{
    return LogBase(base, other);
}


/******************************************************************************/
/* GLOBAL LOG                                                                 */
/******************************************************************************/

extern Log<1024> GlobalLog;

} // lockless

#endif // __lockless__log_h__
