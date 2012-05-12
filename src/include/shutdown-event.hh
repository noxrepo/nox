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
#ifndef SHUTDOWN_EVENT_HH
#define SHUTDOWN_EVENT_HH 1

#include "event.hh"

namespace vigil
{

/** \ingroup noxevents
 *
 * Thrown by NOX prior to system shutdown.
 *
 */

class Shutdown_event
    : public Event
{
public:
    Shutdown_event() : Event(static_get_name()) { }

    /* Currently we don't provide any information on the reason for the
     * shutdown.  FIXME? */

    static const Event_name static_get_name()
    {
        return "Shutdown_event";
    }
};

} // namespace vigil

#endif /* shutdown-event.hh */
