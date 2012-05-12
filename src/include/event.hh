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
#ifndef EVENT_HH
#define EVENT_HH 1

#include <string>
#include <vector>

namespace vigil
{

enum Disposition { CONTINUE, STOP };

typedef std::string Event_name;

/** @defgroup noxevents NOX Events
 *
 * An Event represents a low-level or high-level event in the network.  The
 * Event class exists almost solely to provide a convenient way to pass any
 * kind of event around.  All interesting data exists in Event's derived
 * classes.
 *
 * An Event supplies only information.  It does not know how to process itself,
 * because there are many possible strategies for processing a given event.
 * Processing of an Event is deferred to Handlers
 *
 */

/** @ingroup noxevents
 *
 * Base class for events.
 */

/** @ingroup noxevents
 *
 * Base class for events.
 */
class Event
{
public:
    virtual ~Event() {}

    /* Get event name */
    Event_name get_name() const
    {
        return name;
    }

protected:
    Event(const Event_name& name_) : name(name_) {}

    //void set_name(const Event_name& name_) { name = name_; }

private:
    const Event_name name;
};

} // namespace vigil

#endif /* event.hh */
