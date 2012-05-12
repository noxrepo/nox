/* Copyright 2008, 2009 (C) Nicira, Inc.
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
#ifndef NEW_CONNECTION_EVENT_HH
#define NEW_CONNECTION_EVENT_HH 1

#include "event.hh"
#include "connection.hh"

#include <boost/shared_ptr.hpp>

namespace vigil
{

/** \ingroup noxevents
 *
 * Thrown by Connection_manager upon connection initiation
 *
 */

class New_connection_event
    : public Event
{
public:
    New_connection_event(boost::shared_ptr<Connection> conn)
        : Event(static_get_name()), connection(conn) { }

    static const Event_name static_get_name()
    {
        return "New_connection_event";
    }

    boost::shared_ptr<Connection> connection;
};

} // namespace vigil

#endif /* new-connection-event.hh */
