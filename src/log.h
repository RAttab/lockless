/* log.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 24 Dec 2012
   FreeBSD-style copyright and disclaimer apply

   Lockfree logger used for debugging with minimal lantency.
 */

#ifndef __lockless__log_h__
#define __lockless__log_h__

#include "tls.h"
#include "clock.h"
#include "utils.h"

#include <stdio.h>
#include <array>
#include <vector>
#include <string>
#include <algorithm>
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
    LogMisc  = 0x000,
    LogRcu   = 0x100,
    LogQueue = 0x200,
    LogMap   = 0x300,
    LogAlloc = 0x400,
};

std::string to_string(LogType type);


/******************************************************************************/
/* LOG ENTRY                                                                  */
/******************************************************************************/

struct LogEntry
{
    template<typename Title, typename Msg>
    LogEntry(LogType type, size_t tick, size_t tid, Title&& title, Msg&& msg) :
        type(type),
        tick(tick),
        threadId(tid),
        title(std::forward<Title>(title)),
        msg(std::forward<Msg>(msg))
    {}

    std::string print() const;

    bool operator< (const LogEntry& other) const
    {
        return Clock<size_t>::compare(this->tick, other.tick) < 0;
    }

    bool operator== (const LogEntry& other) const
    {
        return
            type     == other.type &&
            tick     == other.tick &&
            threadId == other.threadId &&
            title    == other.title &&
            msg      == other.msg;
    }

    LogType type;
    size_t tick;
    size_t threadId;
    std::string title;
    std::string msg;
};


/******************************************************************************/
/* LOG                                                                        */
/******************************************************************************/

template<size_t SizeT>
struct Log
{
    locklessEnum size_t Size = SizeT;

    Log() : index(0)
    {
        for (auto& entry : logs) entry.store(nullptr);
    }

    // Dump clears out and cleans up all the entries.
    ~Log() { dump(); }

    Log(const Log& other) = delete;
    Log& operator=(const Log& other) = delete;

    size_t size() const { return Size; }

    template<typename Title, typename Msg>
    void log(LogType type, size_t tick, Title&& title, Msg&& msg)
    {
        LogEntry* entry = new LogEntry(
                type, tick,
                threadId(),
                std::forward<Title>(title),
                std::forward<Msg>(msg));

        size_t i = index.fetch_add(1) % logs.size();
        LogEntry* old = logs[i].exchange(entry);
        if (old) delete old;
    }

    template<typename Title, typename Msg>
    void log(LogType type, Title&& title, Msg&& msg)
    {
        size_t tick = details::GlobalLogClock.tick();
        log(type, tick, std::forward<Title>(title), std::forward<Msg>(msg));
    }

    template<typename Title, typename... Args>
    void operator() (
            LogType type, Title&& title, const char* fmt, const Args&... args)
    {
        log(type, std::forward<Title>(title), format(fmt, args...));
    }


    // The gcc's snprintf warnings get a little overzealous if we try to use the
    // variadic version for a no-args call. Interestingly, it shuts up if we
    // change fmt to a const std::string&. Why? Who the fuck knows...
    template<typename Title, typename... Args>
    void operator() (LogType type, Title&& title, const char* msg)
    {
        log(type, std::forward<Title>(title), std::string(msg));
    }


    std::vector<LogEntry> dump()
    {
        std::vector<LogEntry> dump;
        dump.reserve(logs.size());

        size_t start = index.load();

        for (size_t i = 0; i < logs.size(); ++i) {
            size_t index = (start + i) % logs.size();

            LogEntry* entry = logs[index].exchange(nullptr);
            if (!entry) continue;

            dump.emplace_back(std::move(*entry));
            delete entry;
        }

        // We can't be sure that start will still be the head once we start
        // reading or that a series of log won't overtake our dumps.
        std::sort(dump.begin(), dump.end());

        return dump;
    }

    std::function< std::vector<LogEntry>() > dumpFn()
    {
        return [&] { return this->dump(); };
    }

private:

    std::atomic<size_t> index;
    std::array<std::atomic<LogEntry*>, Size> logs;

};

/******************************************************************************/
/* DEBUG LOG                                                                  */
/******************************************************************************/

template<>
struct Log<0>
{
    locklessEnum size_t Size = 0;

    Log() {}

    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;

    size_t size() const { return 0; }

    template<typename Title, typename Msg>
    void log(LogType, size_t, Title&&, Msg&&)
    {}

    template<typename Title, typename Msg>
    void log(LogType, Title&&, Msg&&)
    {}

    template<typename Title, typename... Args>
    void operator() (LogType, Title&&, const char*, Args&&...)
    {}

    template<typename Title, typename... Args>
    void operator() (LogType, Title&&, const char*)
    {}

    std::vector<LogEntry> dump() { return {}; }

    std::function< std::vector<LogEntry>() > dumpFn()
    {
        return [&] { return this->dump(); };
    }
};

template<size_t Size, bool Flag>
struct DebuggingLog
{
    typedef Log<Flag ? Size : 0> type;
};

namespace { Log<0> NullLog; }


/******************************************************************************/
/* LOG AGGREGATOR                                                             */
/******************************************************************************/

struct LogAggregator
{
    LogAggregator() : totalSize(0) {}

    template<typename... LogRest>
    LogAggregator(LogRest&... rest) : totalSize(0) { add(rest...); }


    size_t size() const { return totalSize; }

    void clear() { logs.clear(); }


    template<typename LogT>
    void add(LogT& log) {
        logs.push_back(log.dumpFn());
        totalSize += log.size();
    }

    template<typename LogT, typename... LogRest>
    void add(LogT& log, LogRest&... rest)
    {
        add(log);
        add(rest...);
    }

    std::vector<LogEntry> dump()
    {
        std::vector<LogEntry> dump;
        dump.reserve(totalSize);

        for (auto& dumpFn : logs) {
            auto d = dumpFn();
            dump.insert(dump.end(), d.begin(), d.end());
        }

        std::sort(dump.begin(), dump.end());
        return dump;
    }

    typedef std::function< std::vector<LogEntry>() > DumpFn;

    DumpFn dumpFn() { return [&] { return this->dump(); }; }

private:
    std::vector<DumpFn> logs;
    size_t totalSize;
};


/******************************************************************************/
/* LOG SINK                                                                   */
/******************************************************************************/

inline void
dumpToStream(
        const std::vector<LogEntry>& dump, std::ostream& stream = std::cerr)
{
    for (const LogEntry& entry : dump)
        stream << (entry.print() + "\n");
    stream << std::flush;
}

template<typename LogT>
void logToStream(LogT& log, std::ostream& stream = std::cerr)
{
    dumpToStream(log.dump(), stream);
}

} // lockless

#endif // __lockless__log_h__
