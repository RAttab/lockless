/* test_checks.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 05 Jan 2013
   FreeBSD-style copyright and disclaimer apply

   Predicate checking utilities for tests.
*/

#ifndef __lockless__test_checks_h__
#define __lockless__test_checks_h__

#include "debug.h"
#include "log.h"

#include <stdio.h>
#include <array>

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

template<typename LogT>
void check(bool pred, const std::string& str, LogT& log, const CheckContext& ctx)
{
    if (pred) return;

    printf( "%s:%d: %s %s\n",
            ctx.file, ctx.line, ctx.function, str.c_str());

    auto dump = log.dump();
    std::reverse(dump.begin(), dump.end());
    dumpToStream(dump);

    if (CheckAbort) abort();
    // \todo could do an exception variant as well.
}

std::string checkStr(const char* pred)
{
    std::array<char, 256> buffer;
    snprintf(buffer.data(), buffer.size(), "{ %s }", pred);
    return std::string(buffer.data());
}


template<typename First, typename Second>
std::string checkStr(
        const char* op,
        const char* first,  const First& firstVal,
        const char* second, const Second& secondVal)
{
    std::array<char, 256> buffer;
    snprintf(buffer.data(), buffer.size(),
            "{ %s %s %s } { %s %s %s }",
            first, op, second,
            std::to_string(firstVal).c_str(), op,
            std::to_string(secondVal).c_str());

    return std::string(buffer.data());
}


/******************************************************************************/
/* CHECK PREDICATES                                                           */
/******************************************************************************/

#define locklessCheckCtx(_pred_, _log_, _ctx_) \
    lockless::check((_pred_), lockless::checkStr(#_pred_), _log_, _ctx_)

#define locklessCheck(_pred_, _log_) \
    locklessCheckCtx(_pred_, _log_, locklessCtx())

#define locklessCheckOpCtx(_op_, _first_, _second_, _log_, _ctx_)       \
    do {                                                                \
        decltype(_first_) firstVal = (_first_);                         \
        decltype(_second_) secondVal = (_second_);                      \
        lockless::check(                                                \
                firstVal _op_ secondVal,                                \
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


} // lockless

#endif // __lockless__test_checks_h__
