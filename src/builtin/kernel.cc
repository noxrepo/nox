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
#include "kernel.hh"

#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>

#include "deployer.hh"
#include "vlog.hh"

using namespace std;
using namespace vigil;

static Vlog_module lg("kernel");

/* Global kernel singleton */
static Kernel* kernel;

state_change_error::state_change_error(const std::string& msg)
    : runtime_error(msg)
{
}

Kernel::Kernel(const std::string& info_file, int argc_, char** argv_)
    : argc(argc_), argv(argv_), state_requirements_changed(false),
      installing(false), start_time(::time(0))
{
    /* Create a new container info file, if necessary. */
    int fd = ::open(info_file.c_str(), O_RDWR | O_CREAT, S_IRWXU);
    if (fd == -1)
    {
        throw runtime_error("Unable to open info file '" + info_file + "'.");
    }

    /* Try to read the file, just in case it happened to exist
       before. */
    int c = ::read(fd, &this->info, sizeof(struct container_info));
    if (c != sizeof(struct container_info))
    {
        info.restart_counter = 0;

        /* Use the non-blocking random generator, since this is not
           that security critical identifier. */
        int fd = open("/dev/urandom", O_RDONLY);
        if (fd == -1 ||
            ::read(fd, &info.uuid, sizeof(UUID)) != sizeof(UUID))
        {
            throw runtime_error("Unable to generate a random UUID.");
        }
        info.uuid &= 0x7fffffffffffffffULL;
        ::close(fd);

        VLOG_DBG(lg, "Assigned a new UUID for the container: %" PRIu64, info.uuid);
    }

    /* Increment the counter and save the changes. */
    ++info.restart_counter;

    ::lseek(fd, 0L, SEEK_SET);
    if (::write(fd, &info, sizeof(struct container_info)) != sizeof(struct
            container_info))
    {
        VLOG_ERR(lg, "Could not write the container info to info file '%s'.",
                 info_file.c_str());
    }
    ::close(fd);
}

time_t
Kernel::uptime() const
{
    return ::time(0) - start_time;
}

Kernel::UUID
Kernel::uuid() const
{
    return info.uuid;
}

uint64_t
Kernel::restarts() const
{
    return info.restart_counter;
}

void
Kernel::set_argument(const Component_name& name,
                     const Component_argument& arg)
{
    arguments[name] = arg;
}

const Component_argument
Kernel::get_argument(const Component_name& name) const
{
    Component_argument_map::const_iterator i = arguments.find(name);
    return i == arguments.end() ? Component_argument() : i->second;
}

void
Kernel::attach_deployer(Deployer* deployer)
{
    deployers.push_back(deployer);
}

Deployer_list
Kernel::get_deployers() const
{
    return deployers;
}

Component_context*
Kernel::get(const Component_name& name) const
{
    Component_name_context_map::const_iterator i = contexts.find(name);
    return i == contexts.end() ? 0 : i->second;
}

Component_context*
Kernel::get(const Component_name& name, const Component_state state) const
{
    Component_name_context_map::const_iterator i = contexts.find(name);
    if (i == contexts.end())
    {
        return 0;
    }
    else
    {
        Component_context* context = i->second;
        return context->get_state() == state ? context : 0;
    }
}

Component_context_list
Kernel::get_all() const
{
    Component_context_list l;

    BOOST_FOREACH(Component_name_context_map::value_type v, contexts)
    {
        l.push_back(v.second);
    }

    return l;
}

void
Kernel::install(const Component_name& name, const Component_state to)
throw(state_change_error)
{
    if (contexts.count(name) != 0)
    {
        throw state_change_error("Application '" + name +
                                 "' was tried to be installed multiple times.");
    }

    BOOST_FOREACH(Deployer* d, deployers)
    {
        if (d->deploy(this, name))
        {
            change(contexts[name], to);
            return;
        }
    }

    throw state_change_error("Application '" + name +
                             "' description not found.");
}

void
Kernel::install(Component_context* ctxt, const Component_state to)
{
    if (contexts.count(ctxt->get_name()) != 0)
    {
        throw state_change_error("component already installed");
    }

    contexts[ctxt->get_name()] = ctxt;

    if (per_state.find(NOT_INSTALLED) == per_state.end())
    {
        per_state[NOT_INSTALLED] = new Component_context_list();
    }

    per_state[NOT_INSTALLED]->push_back(ctxt);

    change(ctxt, to);
}

void
Kernel::change(Component_context* ctxt, const Component_state to)
throw(state_change_error)
{
    const Component_state from = ctxt->get_state();
    ctxt->set_required_state(to);

    if (from == to)
    {
        return;
    }

    if (from > to)
    {
        throw state_change_error("uninstalling not implemented");
    }

    if (contexts.count(ctxt->get_name()) != 1)
    {
        throw state_change_error("unknown context");
    }

    if (installing)
    {
        state_requirements_changed = true;
    }
    else
    {
        resolve();

        if (!get(ctxt->get_name(), to))
        {
            hash_set<Component_context*> c;
            throw state_change_error("Cannot change the state of '" +
                                     ctxt->get_name() + "' to " +
                                     Component_state_string[to] + ":\n" +
                                     ctxt->get_error_message(c));
        }
    }
}

void
Kernel::resolve()
{
    bool progress = installing = true;

    while (progress || state_requirements_changed)
    {
        state_requirements_changed = progress = false;

        for (int i = NOT_INSTALLED; i < INSTALLED && !progress; ++i)
        {
            Component_state from = Component_state(i);
            Component_state to = Component_state(i + 1);
            progress = resolve(from, to);
        }
    }

    installing = false;
}

bool
Kernel::resolve(const Component_state from, const Component_state to)
{
    Component_context_list* to_install = per_state[from];
    if (!to_install)
        return false;

    Component_context_list dependencies_resolved = resolve(*to_install, to);

    BOOST_FOREACH(Component_context* ctxt, dependencies_resolved)
    {
        ctxt->install(to);
        for (auto it = to_install->begin(); it != to_install->end(); ++it)
        {
            if (*it != ctxt)
                continue;
            to_install->erase(it);
            break;
        }

        if (per_state.find(ctxt->get_state()) == per_state.end())
        {
            per_state[ctxt->get_state()] = new Component_context_list();
        }

        BOOST_FOREACH(Component_context* c, *to_install)
        {
            // TODO: verify if we should return false (CHANGE: assert -> if)
            //       and whether we should do any cleanup.
            assert(c != ctxt);
        }
        per_state[ctxt->get_state()]->push_back(ctxt);
    }

    return !dependencies_resolved.empty();
}

Component_context_list
Kernel::resolve(const Component_context_list& contexts,
                const Component_state to)
{
    Component_context_list resolved;

    for (auto it = contexts.begin(); it != contexts.end(); it++)
    {
        Component_context* ctxt = *it;
        if (ctxt->resolve_dependencies(to))
            resolved.push_back(ctxt);
    }

    return resolved;
}

void
Kernel::init(const std::string& info, int argc, char** argv)
{
    kernel = new Kernel(info, argc, argv);
}

Kernel*
Kernel::get_instance()
{
    return kernel;
}
