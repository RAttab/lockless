/* test_checks.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 05 Jan 2013
   FreeBSD-style copyright and disclaimer apply

   Predicate checking utilities for tests.
*/

#ifndef __lockless__test_checks_h__
#define __lockless__test_checks_h__

#include "lock.h"
#include "debug.h"
#include "log.h"
#include "arch.h"

#include <stdio.h>
#include <array>
#include <sstream>
#include <iostream>

namespace lockless {

/******************************************************************************/
/* CHECK CONTEXT                                                              */
/******************************************************************************/

struct CheckContext
{
    const char* file;
    const char* function;
    unsigned line;

    CheckContext(const char* file, const char* function, unsigned line) :
        file(file), function(function), line(line)
    {}
};

#define locklessCtx() \
    CheckContext(__FILE__, __FUNCTION__, __LINE__)


/******************************************************************************/
/* CHECK                                                                      */
/******************************************************************************/

namespace details {

// Required to make sure log is fully dumped before aborting.
extern UnfairLock checkDumpLock;

} // namespace details

template<typename LogT>
void check(const std::string& str, LogT&& log, const CheckContext& ctx)
{
    // While this is not entirely safe, we're about to abort anyway...
    if (CheckAbort) details::checkDumpLock.lock();

    printf( "%s:%d: %s{%ld} %s\n",
            ctx.file, ctx.line, ctx.function, threadId(), str.c_str());

    auto dump = log.dump();
    std::reverse(dump.begin(), dump.end());

    std::stringstream ss;
    dumpToStream(dump, ss);
    std::cerr << ss.str();

    if (CheckAbort) abort();
    // \todo could do an exception variant as well.
}

inline std::string checkStr(const char* pred)
{
    return format("{ %s }", pred);
}


template<typename First, typename Second>
std::string checkStr(
        const char* op,
        const char* first,  const First& firstVal,
        const char* second, const Second& secondVal)
{
    return format("{ %s %s %s } { %s %s %s }",
            first, op, second,
            std::to_string(firstVal).c_str(), op,
            std::to_string(secondVal).c_str());
}


/******************************************************************************/
/* CHECK PREDICATES                                                           */
/******************************************************************************/

#define locklessCheckCtx(_pred_, _log_, _ctx_) \
    do {                                                                \
        bool val = _pred_;                                              \
        if (locklessLikely(val)) break;                                 \
        lockless::check(lockless::checkStr(#_pred_), _log_, _ctx_);     \
    } while(false)                                                      \

#define locklessCheck(_pred_, _log_) \
    locklessCheckCtx(_pred_, _log_, locklessCtx())

#define locklessCheckOpCtx(_op_, _first_, _second_, _log_, _ctx_)       \
    do {                                                                \
        decltype(_first_) firstVal = (_first_);                         \
        decltype(_second_) secondVal = (_second_);                      \
        if (locklessLikely(firstVal _op_ secondVal)) break;             \
        lockless::check(                                                \
                lockless::checkStr(#_op_, #_first_, firstVal, #_second_, secondVal), \
                _log_, _ctx_);                                          \
    } while (false)

#define locklessCheckOp(_op_, _first_, _second_, _log_) \
    locklessCheckOpCtx(_op_, _first_, _second_, _log_, locklessCtx())


#define locklessCheckEq(_first_, _second_, _log_) \
    locklessCheckOp(==, _first_, _second_, _log_)

#define locklessCheckNe(_first_, _second_, _log_) \
    locklessCheckOp(!=, _first_, _second_, _log_)

#define locklessCheckLt(_first_, _second_, _log_) \
    locklessCheckOp(< , _first_, _second_, _log_)

#define locklessCheckLe(_first_, _second_, _log_) \
    locklessCheckOp(<=, _first_, _second_, _log_)

#define locklessCheckGt(_first_, _second_, _log_) \
    locklessCheckOp(> , _first_, _second_, _log_)

#define locklessCheckGe(_first_, _second_, _log_) \
    locklessCheckOp(>=, _first_, _second_, _log_)


template<typename T, typename LogT>
void checkPair(const std::pair<bool, T>& r, LogT& log, const CheckContext& ctx)
{
    locklessCheckCtx(!r.first, log, ctx);
    locklessCheckOpCtx(==, r.second, T(), log, ctx);
}

template<typename T, typename LogT>
void checkPair(
        const std::pair<bool, T>& r, T exp, LogT& log, const CheckContext& ctx)
{
    locklessCheckCtx(r.first, log, ctx);
    locklessCheckOpCtx(==, r.second, exp, log, ctx);
}


/******************************************************************************/
/* SIGNALS                                                                    */
/******************************************************************************/

namespace details {

void installSignalHandler(const std::function<void()>& callback);
void removeSignalHandler();

} // namespace details

struct SignalCheck
{
    template<typename LogT>
    SignalCheck(LogT& log)
    {
        this->log.add(log);

        details::installSignalHandler([&] { logToStream(log); });
    }

    ~SignalCheck()
    {
        details::removeSignalHandler();
    }

private:
    LogAggregator log;
};

} // lockless

#endif // __lockless__test_checks_h__
