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
#ifndef OPENFLOW_DATAPATH_LEAVE_EVENT_HH
#define OPENFLOW_DATAPATH_LEAVE_EVENT_HH 1

#include <boost/shared_ptr.hpp>
#include "event.hh"
#include "openflow-datapath.hh"

namespace vigil
{
namespace openflow
{

/** \ingroup noxevents
 *
 * Openflow_datapath_leave_events are thrown when a switch disconnects from the
 * network.
 *
 */

class Openflow_datapath_leave_event
    : public Event
{
public:
    Openflow_datapath_leave_event(boost::shared_ptr<Openflow_datapath> dp_)
		: Event(static_get_name()), dp(dp_) { }

    static const Event_name static_get_name()
    {
        return "Openflow_datapath_leave_event";
    }

    boost::shared_ptr<Openflow_datapath> dp;

private:
    //Openflow_datapath_leave_event(const Openflow_datapath_leave_event&);
    //Openflow_datapath_leave_event& operator=(const Openflow_datapath_leave_event&);
};

} // namespace openflow
} // namespace vigil

#endif /* openflow-datapath-leave-event.hh */
