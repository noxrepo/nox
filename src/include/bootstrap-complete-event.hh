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
#ifndef BOOTSTRAP_COMPLETE_EVENT_HH
#define BOOTSTRAP_COMPLETE_EVENT_HH 1

#include "event.hh"

namespace vigil
{

struct Bootstrap_complete_event
        : public Event
{
    Bootstrap_complete_event()
        : Event(static_get_name()) { }

    static const Event_name static_get_name()
    {
        return "Bootstrap_complete_event";
    }

private:
    Bootstrap_complete_event(const Bootstrap_complete_event&);
    Bootstrap_complete_event& operator=(const Bootstrap_complete_event&);
};

} // namespace vigil

#endif /* Bootstrap-complete-event.hh */
