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
#include "event-dispatcher.hh"

#include <boost/bind.hpp>
#include <boost/exception/all.hpp>
#include <boost/foreach.hpp>
#include <boost/ptr_container/ptr_list.hpp>
#include <boost/ref.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/timer.hpp>

#include "assert.hh"
#include "new-connection-event.hh"
#include "shutdown-event.hh"
#include "vlog.hh"

using namespace std;

namespace vigil
{

static Vlog_module lg("event-dispatcher");

// TODO: Convert timer to Timer_event

Event_dispatcher::Event_dispatcher(const Component_context* c, size_t n_threads)
    : Component(c), io(/*n_threads*/), work(io), n_threads(n_threads)
{
}

Component*
Event_dispatcher::instantiate(const Component_context* ctxt, size_t n_threads)
{
    return new Event_dispatcher(ctxt, n_threads);
}

void
Event_dispatcher::configure()
{
    register_event(New_connection_event::static_get_name());
    register_event(Shutdown_event::static_get_name());

    // read the order of execution of event handlers
    // for every event defined in the configuration
    namespace pt = boost::property_tree;
    const list<Event_name>& events = ctxt->get_config_list("events");

    BOOST_FOREACH (const Event_name& event_name, events)
    {
        const list<Component_name>& component_list =
            ctxt->get_config_list("events." + event_name);
        int order = 0;
        // for every component under the event
        Component_priority component_priority;
        BOOST_FOREACH (const Component_name& component_name, component_list)
        {
            // add the priority to the dictionary
            component_priority[component_name] = order++;
        }
        priority_map[event_name] = component_priority;
    }

    register_handler(Shutdown_event::static_get_name(),
                     boost::bind(&Event_dispatcher::handle_shutdown, this, _1), 9999);
}

void
Event_dispatcher::install()
{
    for (std::size_t i = 1; i <= n_threads; i++)
    {
        VLOG_DBG(lg, "creating thread %zu", i);
        tg.create_thread(boost::bind(&boost::asio::io_service::run, &io));
    }
}

void
Event_dispatcher::join_all()
{
    tg.join_all();
}

void
Event_dispatcher::post(const Event& event)
{
    io.post(boost::bind(&Event_dispatcher::dispatch, this,
                        boost::cref(event)));
}

std::unique_ptr<boost::asio::deadline_timer>
Event_dispatcher::post(const Callback& callback, const timeval& duration)
{
    namespace pt = boost::posix_time;
    namespace ba = boost::asio;
    pt::time_duration d =
        pt::seconds(duration.tv_sec) + pt::microseconds(duration.tv_usec);

    std::unique_ptr<ba::deadline_timer> dt(new ba::deadline_timer(io, d));
    dt->async_wait(callback);

    return dt;
}

bool
Event_dispatcher::register_event(const Event_name& name)
{
    VLOG_DBG(lg, "Registering event '%s'.", name.c_str());
    if (priority_map.find(name) == priority_map.end())
    {
        Component_priority cp;
        priority_map[name] = cp;
        Call_chain cc;
        call_chain_map[name] = cc;
        return true;
    }
    return false;
}

bool
Event_dispatcher::register_handler(const Component_name& component_name,
                                   const Event_name& event_name,
                                   const Event_handler& h)
{
    if (priority_map.find(event_name) == priority_map.end())
    {
        return false;
    }

    Component_priority& cp = priority_map[event_name];
    if (cp.find(component_name) == cp.end())
    {
        register_handler(event_name, h, 0);
    }
    else
    {
        register_handler(event_name, h, cp[component_name]);
    }

    return true;
}

void
Event_dispatcher::register_handler(const Event_name& event_name,
                                   const Event_handler& handler, int order)
{
    VLOG_DBG(lg, "Registering handler for %s, order %d",
             event_name.c_str(), order);

    //boost::unique_lock<boost::mutex> lock(table_mutex);
    Event_handler_wrapper ehw(handler, order);
    call_chain_map[event_name].insert(ehw);
}

void
Event_dispatcher::dispatch(const Event& event) const
{
    //boost::unique_lock<boost::mutex> lock(table_mutex);
    const Event_name& event_name = event.get_name();
    Call_chain_map::const_iterator it = call_chain_map.find(event_name);
    if (it != call_chain_map.end())
    {
        const Call_chain& call_chain = it->second;
        BOOST_FOREACH (const Event_handler_wrapper& ehw, call_chain)
        {
            try
            {
                if (ehw(event) == STOP)
                {
                    break;
                }
            }
            catch (const exception& e)
            {
                VLOG_ERR(lg, "Event %s processing leaked an exception: %s",
                         event_name.c_str(), e.what());
                VLOG_ERR(lg, "Extra information:\n%s",
                         boost::current_exception_diagnostic_information().c_str());
                break;
            }
        }
    }
}

Disposition
Event_dispatcher::handle_shutdown(const Event& e)
{
    VLOG_ERR(lg, "Shutting down");
    io.stop();
    return CONTINUE;
}

} // namespace vigil
