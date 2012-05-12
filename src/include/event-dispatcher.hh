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
#ifndef EVENTC_HH
#define EVENTC_HH 1

#include <set>
#include <string>
#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/function.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>
#include <boost/unordered_map.hpp>

#include "component.hh"
#include "event.hh"

namespace vigil
{

/**
 * Event dispatcher component is responsible for event management.
 *
 * This class serves three purposes that could really be broken up into
 * separate classes, but that seems pointless right now:
 *
 *   - Associates events with handlers and allows events to be dispatched to
 *     the appropriate handlers.
 *
 *   - Maintains a queue of pending events.
 *
 * One convenient feature of this event dispatcher is that event handlers may
 * block without holding up processing of further events: they will be
 * dispatched by another thread.
 */

typedef boost::function<void(const boost::system::error_code& error)> Callback;

class Event_dispatcher
    : public Component
{
public:
    /* Construct a new component instance. For nox::main() */
    static Component* instantiate(const Component_context*, size_t n_threads);

    void configure();
    void install();

    /* Join the threads until the end of execution */
    void join_all();

    /* Post an event */
    void post(const Event&);

#if 0
    Timer post(const Callback& callback, const timeval& duration)
    {
        return timer_dispatcher.post(callback, duration);
    }

    Timer post(const Callback& callback)
    {
        return timer_dispatcher.post(callback);
    }
#endif

    /* Posts 'callback' to be called after the given 'duration' elapses.  The
     * caller must not destroy the returned Timer, but may use it to cancel or
     * reschedule the timer, up until the point where the timer is actually
     * invoked. */
    std::unique_ptr<boost::asio::deadline_timer>
    post(const Callback& callback, const timeval& duration);

    /* Dispatch 'event' immediately, bypassing the event queue. */
    void dispatch(const Event&) const;

    boost::asio::io_service& get_io_service()
    {
        return io;
    }

    /* Register an event */
    template <typename T>
    inline
    void register_event()
    {
        register_event(T::static_get_name());
    }

    /* Register an event */
    bool register_event(const Event_name&);

    /* Register an event handler */
    bool register_handler(const Component_name&,
                          const Event_name&,
                          const Event_handler&);

    /* Register 'handler' to be called to process each event of the given
     * 'type'.  Multiple handlers may be registered for any 'type', in which
     * case the handlers are called in increasing order of 'order'. */
    void register_handler(const Event_name&,
                          const Event_handler&, int order);
    // TODO: unregister_handler

private:
    Event_dispatcher(const Component_context*, size_t n_threads);

    class Event_handler_wrapper
    {
    public:
        Event_handler_wrapper(const Event_handler& eh, int p)
            : event_handler(eh), priority(p)
        {
        }

        bool operator<(const Event_handler_wrapper& that) const
        {
            return priority < that.priority;
        }

        Disposition operator()(const Event& event) const
        {
            assert(!event_handler.empty());
            return event_handler(event);
        }
    private:
        Event_handler event_handler;
        int priority;
    };

    typedef std::string Event_name;
    typedef boost::unordered_map<Component_name, int> Component_priority;
    typedef std::multiset<Event_handler_wrapper> Call_chain;
    typedef boost::unordered_map<Event_name, Component_priority> Priority_map;
    typedef boost::unordered_map<Event_name, Call_chain> Call_chain_map;

    /* the io_service object and work */
    boost::asio::io_service io;
    boost::asio::io_service::work work;

    /* number of threads to spawn */
    size_t n_threads;
    boost::thread_group tg;

    /* guard concurrent access */
    boost::mutex priority_map_queue_mutex;
    boost::mutex call_chain_mutex;


    // Event_name -> (Component_name -> priority)
    Priority_map priority_map;
    // Event_name -> Call_chain
    Call_chain_map call_chain_map;

    Disposition handle_shutdown(const Event&);
};

} // namespace vigil

#endif
