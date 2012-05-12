/* Copyright 2008 (C) Nicira, Inc.
 *
 * This file is part of NOX.
 *
 * NOX is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * NOX is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with NOX.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <config.h>
#include "fault.hh"
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cxxabi.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>

#include <boost/format.hpp>

#define ARRAY_SIZE(ARRAY) (sizeof ARRAY / sizeof *ARRAY)

namespace vigil
{

/* Did we register the fault handlers? */
static bool registered;

/* Thread-specific key for keeping track of the signal stack. */
static pthread_key_t signal_stack_key;

/* Return a backtrace. */
std::string
dump_backtrace()
{
    std::string trace;

    /* During the loop:

       frame[0] points to the next frame.
       frame[1] points to the return address. */
    for (void** frame = (void**) __builtin_frame_address(0);
            frame != NULL &&
            frame[0] != NULL && frame != frame[0];
            frame = (void**) frame[0])
    {
        /* Translate return address to symbol name. */
        Dl_info addrinfo;
        if (!dladdr(frame[1], &addrinfo) || !addrinfo.dli_sname)
        {
            trace += boost::str(boost::format("  0x%08"PRIxPTR"\n")
                                % (uintptr_t) frame[1]);
            continue;
        }

        /* Demangle symbol name. */
        const char *name = addrinfo.dli_sname;
        char* demangled_name = NULL;
        int err;
        demangled_name = __cxxabiv1::__cxa_demangle(name, 0, 0, &err);
        if (!err)
        {
            name = demangled_name;
        }

        /* Calculate size of stack frame. */
        void** next_frame = (void**) frame[0];
        size_t frame_size = (char*) next_frame - (char*) frame;

        /* Print. */
        trace +=
            boost::str(boost::format("  0x%08"PRIxPTR" %4u (%s+0x%x)\n")
                       % (uintptr_t) frame[1] % frame_size % name
                       % ((char*) frame[1] - (char*) addrinfo.dli_saddr));

        free(demangled_name);
    }

    return trace;
}

/* Attempt to demangle a undefined symbol name in an error message.
   The undefined symbol is expected to have a prefix 'undefined
   symbol: ' */
std::string demangle_undefined_symbol(const std::string& cause)
{
    using namespace std;

    static const string undef_symbol = "undefined symbol: ";
    string::size_type pos_start = cause.find(undef_symbol);

    if (pos_start == string::npos)
    {
        return cause;
    }

    pos_start += undef_symbol.length();

    string::size_type pos_end = cause.find(" ", pos_start + 1);
    pos_end =
        pos_end == string::npos ? cause.find("\n", pos_start + 1) : pos_end;
    pos_end =
        pos_end == string::npos ? cause.find("\t", pos_start + 1) : pos_end;

    const string symbol_name = pos_end == string::npos ?
                               cause.substr(pos_start) :
                               cause.substr(pos_start, pos_end - pos_start);
    const string rest = pos_end == string::npos ? "" : cause.substr(pos_end);

    size_t demangled_length;
    int status;
    char *demangled =
        __cxxabiv1::__cxa_demangle(symbol_name.c_str(), 0,
                                   &demangled_length, &status);
    const string demangled_symbol_name = demangled ? demangled : symbol_name;
    free(demangled);
    return cause.substr(0, pos_start) + demangled_symbol_name + rest;
}

void fault_handler(int sig_nr)
{
    fprintf(stderr, "Caught signal %d.\n", sig_nr);
    const std::string trace = dump_backtrace();
    fprintf(stderr, "%s", trace.c_str());
    fflush(stderr);
    signal(sig_nr, SIG_DFL);
    raise(sig_nr);
}

static void
setup_signal(int signo)
{
    struct sigaction sa;
    sa.sa_handler = fault_handler;
    sigfillset(&sa.sa_mask);
    sa.sa_flags = SA_ONSTACK;
    if (sigaction(signo, &sa, NULL))
    {
        fprintf(stderr, "sigaction(%d) failed: %s\n", signo, strerror(errno));
    }
}

/* Register a signal stack for the current thread. */
void
create_signal_stack()
{
    if (!registered)
    {
        /* Don't waste memory on a signal stack if we haven't registered
         * signal handlers anyway.  (Also, we haven't initialized
         * signal_stack_key.) */
        return;
    }

    stack_t oss;
    if (!sigaltstack(NULL, &oss) && !(oss.ss_flags & SS_DISABLE))
    {
        /* Already have a signal stack.  Don't create a new one. */
        return;
    }

    stack_t ss;
    /* ss_sp is char* in BSDs and not void* as typically. */
    ss.ss_sp = (char *)malloc(SIGSTKSZ);
    ss.ss_size = SIGSTKSZ;
    ss.ss_flags = 0;
    if (!ss.ss_sp)
    {
        fprintf(stderr, "sigaltstack failed: %s\n", strerror(errno));
    }
    else if (sigaltstack(&ss, NULL))
    {
        fprintf(stderr, "Failed to allocate %zu bytes for signal stack: %s\n",
                (size_t) SIGSTKSZ, strerror(errno));
    }

    /* Copy a pointer to the signal stack into a thread-local variable.  Then,
     * if a leak detector such as "valgrind --leak-check=full" is used, the
     * signal stack will show up as a leak if and only if the thread is
     * terminated without calling free_signal_stack(). */
    pthread_setspecific(signal_stack_key, ss.ss_sp);
}

/* Destroy the thread's signal stack. */
void
free_signal_stack()
{
    if (!registered)
    {
        /* We haven't registered signal handlers, so there's no way that we
         * could have a signal stack to free. */
        return;
    }

    stack_t ss;
    memset(&ss, 0, sizeof ss);
    ss.ss_flags = SS_DISABLE;

    stack_t oss;
    if (!sigaltstack(&ss, &oss) && !(oss.ss_flags & SS_DISABLE))
    {
        pthread_setspecific(signal_stack_key, NULL);
        free(oss.ss_sp);
    }
}

void register_fault_handlers()
{
    if (registered)
    {
        return;
    }
    registered = true;

    if (int error = pthread_key_create(&signal_stack_key, free))
    {
        fprintf(stderr, "pthread_key_create");
        fprintf(stderr, " (%s)", strerror(error));
        exit(EXIT_FAILURE);
    }
    setup_signal(SIGABRT);
    setup_signal(SIGBUS);
    setup_signal(SIGFPE);
    setup_signal(SIGILL);
    setup_signal(SIGSEGV);
}

} // namespace vigil
