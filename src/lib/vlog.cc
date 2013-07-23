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
#include "vlog.hh"
#include <algorithm>
#include <boost/foreach.hpp>
#include <boost/tokenizer.hpp>
#include <errno.h>
#include <string>
#include <stdio.h>
#include <syslog.h>
#include <vector>
#include "hash_map.hh"
#include "string.hh"

namespace vigil
{

// this may be broken into multiple calls to syslog, each of
// size no larger than LINE_MAX
static const int LOG_BUFFER_LEN = 8192;

// in order to know that we don't pass syslog() any string that
// is too long, we must specify a max length for elements in the message
static const int MAX_MODULE_NAME_LEN = 64;
static const int MAX_MSG_NUM_LEN = 5; //also hardcoded below in format
static const int MAX_LEVEL_DESC_LEN = 16;


/*
  This logic is too aggressive for our debian systems that use sysklogd.
  From the sysklogd source, it appears that they limit the format string passed
  in (our entire message) to 1024 bytes, which can be substantially smaller
  than LINE_MAX.  We conservatively hardcode this to 900 for now, which
  seems to avoid truncation.

static const int MAX_MSG_LEN = LINE_MAX - MAX_MODULE_NAME_LEN - \
                               MAX_MSG_NUM_LEN - MAX_LEVEL_DESC_LEN - \
                               5; // for extra formating chars and newline
*/
static const int MAX_MSG_LEN = 900;

// NOTE: no names in this list should be longer than MAX_LEVEL_DESC_LEN
static const char* level_names[Vlog::N_LEVELS] =
{
    "EMER",
    "ERR",
    "WARN",
    "INFO",
    "DBG"
};

const char*
Vlog::get_level_name(Level level)
{
    assert(level < N_LEVELS);
    return level_names[level];
}

Vlog::Level
Vlog::get_level_val(const char* name)
{
    for (Level level = (Level) 0; level < N_LEVELS; level = level + 1)
    {
        if (!strcasecmp(level_names[level], name))
        {
            return level;
        }
    }
    return (Level) - 1;
}

static const char* facility_names[Vlog::N_LEVELS] =
{
    "syslog",
    "console"
};

const char*
Vlog::get_facility_name(Facility facility)
{
    assert(facility >= 0 && facility < N_FACILITIES);
    return facility_names[facility];
}

Vlog::Facility
Vlog::get_facility_val(const char* name)
{
    for (Facility facility = 0; facility < N_FACILITIES; ++facility)
    {
        if (!strcasecmp(facility_names[facility], name))
        {
            return facility;
        }
    }
    return -1;
}

Vlog&
vlog()
{
    static Vlog* the_vlog = new Vlog();
    return *the_vlog;
}

typedef hash_map<std::string, Vlog::Module> Name_to_module;
typedef hash_map<Vlog::Level*, Vlog::Module> Cache_map;

struct Vlog_impl
{
    int msg_num;

    /* Module names. */
    Name_to_module name_to_module;
    std::vector<std::string> module_to_name;
    size_t n_modules()
    {
        return module_to_name.size();
    }

    /* levels[facility][module] is the log level for 'module' on 'facility'. */
    std::vector<Vlog::Level> levels[Vlog::N_FACILITIES];
    Vlog::Level min_loggable_level(Vlog::Module);

    /* default_levels[facility] is the log level for new modules on
     * 'facility' */
    Vlog::Level default_levels[Vlog::N_FACILITIES];

    /* Map to a module, from a cached value for the minimum level needed to log
     * that module.  (The level pointer is unique but the module need not be,
     * hence the "backward" mapping.) */
    Cache_map min_level_caches;
    void revalidate_cache_entry(const Cache_map::value_type&);
    void revalidate_cache();
};

/* Returns the minimum logging level necessary for a message to the given
 * 'module' to yield output on any logging facility. */
Vlog::Level
Vlog_impl::min_loggable_level(Vlog::Module module)
{
    assert(module < n_modules());

    Vlog::Level min_level = Vlog::LEVEL_EMER;
    for (Vlog::Facility facility = 0; facility < Vlog::N_FACILITIES;
         ++facility)
    {
        min_level = std::max(min_level, levels[facility][module]);
    }
    return min_level;
}

/* Re-validates the minimum logging level for the given cache 'entry'. */
void
Vlog_impl::revalidate_cache_entry(const Cache_map::value_type& entry)
{
    Vlog::Level* cached_min_level = entry.first;
    Vlog::Module module = entry.second;
    *cached_min_level = min_loggable_level(module);
}

/* Re-validates the minimum logging level for every cached entry. */
void
Vlog_impl::revalidate_cache()
{
    BOOST_FOREACH(const Cache_map::value_type& entry, min_level_caches)
    {
        revalidate_cache_entry(entry);
    }
}

const char*
Vlog::get_module_name(Module module)
{
    assert(module < pimpl->module_to_name.size());
    return pimpl->module_to_name[module].c_str();
}

Vlog::Module
Vlog::get_module_val(const char* name, bool create)
{
    std::string short_name = std::string(name).substr(0, MAX_MODULE_NAME_LEN);
    Name_to_module::iterator i = pimpl->name_to_module.find(short_name);
    if (i == pimpl->name_to_module.end())
    {
        if (!create)
        {
            return -1;
        }

        /* Create new module. */
        Module module = pimpl->module_to_name.size();
        pimpl->module_to_name.push_back(short_name);
        Name_to_module::value_type value(short_name, module);
        i = pimpl->name_to_module.insert(value).first;

        /* Set log levels. */
        for (Facility facility = 0; facility < N_FACILITIES; ++facility)
        {
            pimpl->levels[facility].push_back(pimpl->default_levels[facility]);
        }
    }
    return i->second;
}

static void
set_facility_level(Vlog_impl* pimpl,
                   Vlog::Facility facility, Vlog::Module module,
                   Vlog::Level level)
{
    assert(facility < Vlog::N_FACILITIES);
    assert(level < Vlog::N_LEVELS);

    if (module == Vlog::ANY_MODULE)
    {
        Vlog::Level& default_level = pimpl->default_levels[facility];
        default_level = level;
        pimpl->levels[facility].assign(pimpl->n_modules(), default_level);
    }
    else
    {
        pimpl->levels[facility][module] = level;
    }
}

void
Vlog::set_levels(Facility facility, Module module, Level level)
{
    assert(facility < N_FACILITIES || facility == ANY_FACILITY);
    if (facility == ANY_FACILITY)
    {
        for (Facility facility = 0; facility < N_FACILITIES; ++facility)
        {
            set_facility_level(pimpl, facility, module, level);
        }
    }
    else
    {
        set_facility_level(pimpl, facility, module, level);
    }
    pimpl->revalidate_cache();
}

bool
Vlog::is_loggable(Module module, Level level)
{
    assert(module < pimpl->n_modules());
    for (Facility facility = 0; facility < N_FACILITIES; ++facility)
    {
        if (level <= pimpl->levels[facility][module])
        {
            return true;
        }
    }
    return false;
}

/* Returns the minimum logging level necessary for a message to the given
 * 'module' to yield output on any logging facility. */
Vlog::Level
Vlog::min_loggable_level(Module module)
{
    return pimpl->min_loggable_level(module);
}

Vlog::Vlog()
    : pimpl(new Vlog_impl)
{
    /* This only need to be done once per program, but doing it more than once
     * shouldn't hurt. */
    ::openlog("nox", LOG_NDELAY, 0);

    pimpl->msg_num = 0;
    for (Facility facility = 0; facility < N_FACILITIES; ++facility)
    {
        pimpl->default_levels[facility] = LEVEL_WARN;
    }

    /* Create an initial module with value 0 so that no real module has that
     * value.  If any messages are logged by a statically defined Vlog_module
     * before the Vlog_module's constructor is called, then its 'module' will
     * be 0, so that its module name will be logged as "uninitialized". */
    get_module_val("uninitialized");


    // Init socket to forward log msgs
    hSock = socket(AF_INET, SOCK_DGRAM, 0);
    struct hostent* pServer = gethostbyname("localhost");
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr.s_addr, pServer->h_addr, pServer->h_length);
    addr.sin_port = htons(2222);

}

Vlog::~Vlog()
{
    delete pimpl;
}

void
Vlog::log(Module module, Level level, const char* format, ...)
{
    if (!is_loggable(module, level))
    {
        return;
    }

    va_list arg;
    char logMsg[LOG_BUFFER_LEN];
    ::va_start(arg, format);
    ::vsnprintf(logMsg, sizeof logMsg, format, arg);
    ::va_end(arg);
    output(module, level, logMsg);
}

template <typename T, typename I>
static
void
iter_inc_throw_on_end(const T& t, I& i, const std::string& str, bool end)
{
    ++i;
    if (end != (i == t.end()))
    {
        throw std::runtime_error(str);
    }
}


//-----------------------------------------------------------------------------
//  Set debugging levels:
//
//  mod:facility:level mod2:facility:level
std::string
Vlog::set_levels_from_string(const std::string& str)
{
    using namespace std;
    using namespace boost;

    char_separator<char> sepwhite(" \t");
    char_separator<char> sepcol(":");

    typedef tokenizer<char_separator<char> > charseptok;

    charseptok tok(str, sepwhite);

    for (charseptok::iterator beg = tok.begin(); beg != tok.end(); ++beg)
    {
        charseptok curmod(*beg, sepcol);

        /* Parse module. */
        charseptok::iterator moditer = curmod.begin();
        Module module;
        if (*moditer == "ANY")
        {
            module = ANY_MODULE;
        }
        else
        {
            module = get_module_val(moditer->c_str(), true);
            if (module == -1)
            {
                return "unknown module " + *moditer;
            }
        }

        /* Parse facility. */
        iter_inc_throw_on_end(curmod, moditer, "invalid token " + *beg, false);
        Facility facility;
        if (*moditer == "ANY")
        {
            facility = ANY_FACILITY;
        }
        else
        {
            facility = get_facility_val((*moditer).c_str());
            if (facility == -1)
            {
                return "unknown facility " + *moditer;
            }
        }

        /* Parse level. */
        iter_inc_throw_on_end(curmod, moditer, "invalid token " + *beg, false);
        Level level = get_level_val((*moditer).c_str());
        if (level == -1)
        {
            return "unknown level " + *moditer;
        }
        iter_inc_throw_on_end(curmod, moditer, "invalid token " + *beg, true);

        /* Set level. */
        set_levels(facility, module, level);
    }
    return "ack";
}

std::string
Vlog::get_levels()
{
    std::string levels;
    levels += "                 console    syslog\n";
    levels += "                 -------    ------\n";
    for (size_t i = 0; i < pimpl->n_modules() ; i++)
    {
        string_printf(
            levels,
            "%-16s  %4s       %4s\n",
            pimpl->module_to_name[i].c_str(),
            get_level_name(pimpl->levels[FACILITY_CONSOLE][i]),
            get_level_name(pimpl->levels[FACILITY_SYSLOG][i]));
    }
    return levels;
}

void
Vlog::output(Module module, Level level, const char* log_msg)
{
    pimpl->msg_num++;

    int save_errno = errno;

    const char* module_name = get_module_name(module);
    const char* level_name = get_level_name(level);
    if (pimpl->levels[FACILITY_CONSOLE][module] >= level)
    {
        size_t length = strlen(log_msg);
        bool needs_new_line = !length || log_msg[length - 1] != '\n';
        ::fprintf(stderr, "%05d|%s|%s:%s%s",
                  pimpl->msg_num, module_name, level_name, log_msg,
                  needs_new_line ? "\n" : "");
    }

    if (pimpl->levels[FACILITY_SYSLOG][module] >= level)
    {
        int priority
        = (level == LEVEL_EMER ? LOG_EMERG
           : level == LEVEL_ERR ? LOG_ERR
           : level == LEVEL_WARN ? LOG_WARNING
           : level == LEVEL_INFO ? LOG_INFO
           : LOG_DEBUG);
        if (strlen(log_msg) < MAX_MSG_LEN)
        {
            ::syslog(priority, "%05d|%s:%s %s",
                     pimpl->msg_num, module_name, level_name, log_msg);
        }
        else
        {
            // this is a long message, so we will need to split it into multiple calls
            // to syslog, each of length <= MAX_MSG_LEN
            // Do this in a separate branch to avoid copies on short log messages
            std::string long_str = std::string(log_msg);
            int start_index = 0;
            while (start_index < long_str.length())
            {
                std::string sub = long_str.substr(start_index, MAX_MSG_LEN);
                ::syslog(priority, "%05d|%s:%s %s",
                         pimpl->msg_num, module_name, level_name, sub.c_str());
                start_index += MAX_MSG_LEN;
            }
        }
    }

    // Send log msg to gui socket
    char pWrite[MAX_MSG_LEN];
    snprintf(pWrite, MAX_MSG_LEN, "%05d|%s|%s:%s\n", pimpl->msg_num, module_name, level_name, log_msg);
    sendto(hSock, pWrite, strlen(pWrite), 0, (sockaddr*)&addr, sizeof(addr));

    /* Restore errno (it's pretty unfriendly for a log function to change
     * errno). */
    errno = save_errno;
}

/* Sets up '*cached_min_level' so that it will always be assigned the minimum
 * logging level for output to 'module' to actually log to at least one
 * facility.  'cached_min_level' must not already be in use as a level
 * cache. */
void
Vlog::register_cache(Vlog::Module module, Level* cached_min_level)
{
    Cache_map::value_type entry(cached_min_level, module);
    bool unique = pimpl->min_level_caches.insert(entry).second;
    assert(unique);

    pimpl->revalidate_cache_entry(entry);
}

/* Removes 'cached_min_level' from use as a level cache.  'cached_min_level'
 * must have previously been set up as a level cache with register_cache(). */
void
Vlog::unregister_cache(Level* cached_min_level)
{
    bool deleted = pimpl->min_level_caches.erase(cached_min_level);
    assert(deleted);
}

#define VLOG_MODULE_DO_LOG(LEVEL)                       \
    if (is_enabled(LEVEL)) {                            \
        int save_errno = errno;                         \
        va_list args;                                   \
        va_start(args, format);                         \
        char msg[LOG_BUFFER_LEN];                       \
        ::vsnprintf(msg, sizeof msg, format, args);     \
        vlog().output(module, (LEVEL), msg);            \
        va_end(args);                                   \
        errno = save_errno;                             \
    }

Vlog_module::Vlog_module(const char* module_name)
    : module(vlog().get_module_val(module_name))
{
    vlog().register_cache(module, &cached_min_level);
}

Vlog_module::~Vlog_module()
{
    vlog().unregister_cache(&cached_min_level);
}

void Vlog_module::emer(const char* format, ...)
{
    VLOG_MODULE_DO_LOG(Vlog::LEVEL_EMER);
}

void Vlog_module::err(const char* format, ...)
{
    VLOG_MODULE_DO_LOG(Vlog::LEVEL_ERR);
}

void Vlog_module::warn(const char* format, ...)
{
    VLOG_MODULE_DO_LOG(Vlog::LEVEL_WARN);
}

void Vlog_module::info(const char* format, ...)
{
    VLOG_MODULE_DO_LOG(Vlog::LEVEL_INFO);
}

void Vlog_module::dbg(const char* format, ...)
{
    VLOG_MODULE_DO_LOG(Vlog::LEVEL_DBG);
}

void Vlog_module::log(int level, const char* format, ...)
{
    VLOG_MODULE_DO_LOG(level);
}

} // namespace vigil
