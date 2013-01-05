/* test_checks.h                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 05 Jan 2013
   FreeBSD-style copyright and disclaimer apply

   Predicate checking utilities for tests.
*/

#ifndef __lockless__test_checks_h__
#define __lockless__test_checks_h__

#include "debug.h"

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

enum CheckBehaviour { Abort, Print };

void check(bool pred, const std::string& str, const CheckContext& ctx)
{
    if (pred) return;

    printf( "%s:%d: %s %s\n",
            ctx.file, ctx.line, ctx.function, str.c_str());

    if (CheckAbort) abort();
}

template<typename First, typename Second>
std::string checkStringify(
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

#define locklessCheckCtx(_pred_, _ctx_)                  \
    check((_pred_), (#_pred_), _ctx_)

#define locklessCheck(_pred_) \
    locklessCheckCtx(_pred_, locklessCtx())

#define locklessCheckOpCtx(_op_, _first_, _second_, _ctx_)       \
    check(  (_first_) _op_ (_second_),                                  \
            checkStringify(#_op_, #_first_, _first_, #_second_, _second_), \
            _ctx_)

#define locklessCheckOp(_op_, _first_, _second_) \
    locklessCheckOpCtx(_op_, _first_, _second_, locklessCtx())


#define locklessCheckEq(_first_, _second_) \
    locklessCheckOp(==, _first_, _second_)

#define locklessCheckLt(_first_, _second_) \
    locklessCheckOp(< , _first_, _second_)

#define locklessCheckLe(_first_, _second_) \
    locklessCheckOp(<=, _first_, _second_)

#define locklessCheckGt(_first_, _second_) \
    locklessCheckOp(> , _first_, _second_)

#define locklessCheckGe(_first_, _second_) \
    locklessCheckOp(>=, _first_, _second_)


template<typename T>
void checkPair(const std::pair<bool, T>& r, const CheckContext& ctx)
{
    locklessCheckCtx(!r.first, ctx);
    locklessCheckOpCtx(==, r.second, T(), ctx);
}

template<typename T>
void checkPair(const std::pair<bool, T>& r, T exp, const CheckContext& ctx)
{
    locklessCheckCtx(r.first, ctx);
    locklessCheckOpCtx(==, r.second, exp, ctx);
}


} // lockless

#endif // __lockless__test_checks_h__
