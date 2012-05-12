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
#include "component.hh"

#include <sstream>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "errors.hh"
#include "event-dispatcher.hh"
#include "kernel.hh"
#include "vlog.hh"

using namespace std;

namespace vigil
{
static Vlog_module lg("kernel");

namespace fs = boost::filesystem;
namespace pt = boost::property_tree;

static string indent(string s)
{
    static const string CRLF("\n");
    for (size_t pos = s.find(CRLF); pos != -1; pos = s.find(CRLF, pos + 1))
    {
        s.replace(pos, CRLF.length(), "\n\t");
    }

    return s;
}

Component::Component(const Component_context* ctxt)
    : ctxt(ctxt)
{
    if (ctxt->get_name() == "event-dispatcher")
    {
        event_dispatcher = static_cast<Event_dispatcher*>(this);
    }
    else
    {
        event_dispatcher = dynamic_cast<Event_dispatcher*>(
                               ctxt->get_by_name("event-dispatcher"));
    }
    assert(event_dispatcher != NULL);
}

Component::~Component()
{
}

void
Component::dispatch(const Event& event) const
{
    event_dispatcher->dispatch(event);
}

void
Component::post(const Event& event) const
{
    event_dispatcher->post(event);
}

void
Component::register_event(const Event_name& name) const
{
    if (!event_dispatcher->register_event(name))
    {
        throw runtime_error("Event '" + name + "' already exists.");
    }
}

void
Component::register_handler(const Event_name& event_name,
                            const Event_handler& h) const
{
    if (!event_dispatcher->register_handler(ctxt->get_name(), event_name, h))
    {
        throw runtime_error("Event '" + event_name + "' doesn't exist.");
    }
}

Component_context::Component_context(const Component_name& name,
                                     const std::string& config_path)
    : name(name),
      current_state(NOT_INSTALLED),
      required_state(NOT_INSTALLED),
      component(0),
      kernel(Kernel::get_instance())
{
    // resolve component directory based on config file
    fs::path p = fs::path(config_path);
    if (fs::exists(p))
    {
        pt::ptree root;
        pt::read_json(config_path, root);
        properties = root.get_child(name);
        // populate home_path
        properties.put("home_path", p.branch_path().string());
        // populate commandline args (already separated
        const Component_argument& arg = kernel->get_argument(name);
        if (!arg.empty())
        {
            // parse it as json
            if (arg[0] == '{')
            {
                std::stringstream ss(arg);
                pt::ptree args_ptree;
                pt::read_json(ss, args_ptree);
                properties.put_child("args", args_ptree);
            }
            // parse it as key=value
            else
            {
                std::vector<std::string> args;
                // use comma as the separator
                boost::split(args, arg, boost::is_any_of(","));
                BOOST_FOREACH (const std::string& str, args)
                {
                    std::string key("args.");
                    // use comma as the separator
                    std::vector<std::string> kv;
                    boost::split(kv, str, boost::is_any_of(","));
                    key += kv[0];
                    properties.put(key, kv.size() > 1 ? kv[1] : "");
                }
            }
        }
        // TODO: populate command line args in the ptree
    }
}

Component_context::~Component_context()
{
}

void
Component_context::install(const Component_state to_state)
{
    if (install_actions[to_state] == 0)
    {
        throw state_change_error("no action to move to state " +
                                 boost::lexical_cast<string>(to_state));
    }

    install_actions[to_state]();
}

void
Component_context::uninstall(const Component_state to_state)
{
    throw state_change_error("uninstall not implemented");
}

bool
Component_context::resolve_dependencies(Component_state state)
{
    BOOST_FOREACH (Dependency* d, dependencies)
    {
        if (!d->resolve(state))
        {
            return false;
        }
    }

    return true;
}

Component_name
Component_context::get_name() const
{
    return name;
}

Interface_description
Component_context::get_interface() const
{
    return interface;
}

Component*
Component_context::get_instance() const
{
    return component;
}

Component_state
Component_context::get_state() const
{
    return current_state;
}

Component_state
Component_context::get_required_state() const
{
    return required_state;
}

void
Component_context::set_required_state(Component_state to)
{
    required_state = to;
}

string
Component_context::get_status() const
{
    string status;

    status += "\tCurrent state: " + Component_state_string[current_state] + "\n";
    status += "\tRequired state: " + Component_state_string[required_state] + "\n";
    status += "\tDependencies: ";
    int i = 0;
    BOOST_FOREACH(Dependency* d, dependencies)
    {
        status += i++ == 0 ? "" : ", ";
        status += d->get_status();
    }
    status += "\n";

    if (current_state == ERROR)
    {
        status += indent(indent("\tError:\n" + error_message)) +
                  "\n";
    }
    return status;
}

Component*
Component_context::resolve_by_name(const Component_name& name) const
{
    Component_context* ctxt = kernel->get(name);
    // Hopefully it is there but not loaded yet
    if (!ctxt)
    {
        kernel->install(name, INSTALLED);
        ctxt = kernel->get(name);
    }
    return ctxt ? ctxt->get_instance() : NULL;
}

Component*
Component_context::get_by_name(const Component_name& name) const
{
    Component_context* context = kernel->get(name, INSTALLED);
    return context ? context->get_instance() : 0;
}

Component*
Component_context::get_by_interface(const Interface_description& i) const
{
    BOOST_FOREACH(Component_context* ctxt, kernel->get_all())
    {
        if (ctxt->get_state() == INSTALLED && i == ctxt->get_interface())
        {
            return ctxt->get_instance();
        }
    }

    return 0;
}

Kernel*
Component_context::get_kernel() const
{
    return kernel;
}

string
Component_context::get_error_message(hash_set<Component_context*> ctxts_visited)
const
{
    if (current_state == ERROR)
    {
        return "'" + name + "' ran into an error: " +
               indent("\n" + error_message);
    }

    if (ctxts_visited.find(const_cast<Component_context*>(this)) !=
            ctxts_visited.end())
    {
        return "A circular component dependency detected.";
    }

    ctxts_visited.insert(const_cast<Component_context*>(this));

    BOOST_FOREACH (Dependency* d, dependencies)
    {
        string err = d->get_error_message(ctxts_visited);
        if (err != "")
        {
            return "'" + name + "' has an unmet dependency: " + err;
        }
    }

    return "";
}

const pt::ptree&
Component_context::get_config_child(const std::string& key) const
{
    return properties.get_child(key);
}

std::list<std::string>
Component_context::get_config_list(const std::string& key) const
{
    const pt::ptree& pt = properties.get_child(key);
    std::list<std::string> l;
    BOOST_FOREACH (const pt::ptree::value_type& item, pt)
    {
        l.push_back(item.first);
    }
    return l;
}

const bool
Component_context::has(const std::string& key) const
{
    try
    {
        properties.get_child(key);
        return true;
    }
    catch(...)
    {
        return false;
    }
}


Component_factory::Component_factory()
{
}

Component_factory::~Component_factory()
{
}

Dependency::~Dependency()
{
}

Name_dependency::Name_dependency(const Component_name& name_)
    : name(name_)
{
}

bool
Name_dependency::resolve(const Component_state to_state)
{
    Kernel* kernel = Kernel::get_instance();
    Component_context* ctxt = kernel->get(name);
    if (!ctxt)
    {
        kernel->install(name, INSTALLED);
        return false;
    }
    else
    {
        return ctxt->get_state() == INSTALLED;
    }
}

string
Name_dependency::get_status() const
{
    Component_context* ctxt = Kernel::get_instance()->get(name);
    if (!ctxt)
    {
        return "'" + name + "' not found";
    }

    return ctxt->get_state() == INSTALLED ?
           "'" + name + "' OK" :
           "'" + name + "' not installed";
}

string
Name_dependency::get_error_message(hash_set<Component_context*> ctxts_visited)
const
{
    Component_context* ctxt = Kernel::get_instance()->get(name);
    if (!ctxt)
    {
        return "unmet dependency to '" + name + "': component not found!";
    }

    if (ctxt->get_state() == INSTALLED)
    {
        return "";
    }

    return indent("\n" + ctxt->get_error_message(ctxts_visited));
}

} // namespace vigil
