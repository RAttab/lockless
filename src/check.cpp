/* check.cpp                                 -*- C++ -*-
   Rémi Attab (remi.attab@gmail.com), 06 Feb 2013
   FreeBSD-style copyright and disclaimer apply

   Description
*/

#include <signal.h>
#include <errno.h>
#include <cassert>
#include <cstring>

#include "check.h"

using namespace std;

namespace lockless {

/******************************************************************************/
/* SIGNAL CHECK                                                               */
/******************************************************************************/

namespace {

struct
{
    struct sigaction oldact;
    function<void()> callback;

} sigconfig;


void signalAction(int sig, siginfo_t* info, void* ctx)
{
    printf("SIGSEGV handler triggered\n");

    sigconfig.callback();

    if (sigconfig.oldact.sa_sigaction)
        sigconfig.oldact.sa_sigaction(sig, info, ctx);

    else if (sigconfig.oldact.sa_handler)
        sigconfig.oldact.sa_handler(sig);
}

} // namespace anonymous

namespace details {

void installSignalHandler(const std::function<void()>& callback)
{
    assert(!sigconfig.callback);

    sigconfig.callback = callback;

    struct sigaction act;
    act.sa_sigaction = &signalAction;
    act.sa_flags = SA_SIGINFO;

    int err = sigaction(SIGSEGV, &act, &sigconfig.oldact);
    if (!err) return;

    cerr << "Unable to install signal handler: " << strerror(errno) << endl;
    abort();
}

void removeSignalHandler()
{
    assert(sigconfig.callback);

    int err = sigaction(SIGSEGV, &sigconfig.oldact, 0);
    if (!err) {
        sigconfig.callback = function<void()>();
        return;
    }

    cerr << "Unable to remove signal handler: " << strerror(errno) << endl;
    abort();
}

} // namespace details

} // lockless
