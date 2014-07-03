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
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
#ifndef OFP_MSG_EVENT_HH
#define OFP_MSG_EVENT_HH

#include <boost/shared_ptr.hpp>

#include "event.hh"
#include "openflow-datapath.hh"
#include "port.hh"


namespace vigil
{
namespace openflow
{

class Openflow_event : public Event
{
public:
    Openflow_event(boost::shared_ptr<Openflow_datapath> dp_,
        std::string name_, const struct ofp_header *oh_):
        Event(name_),dp(dp_),oh(oh_)
    {}

    ~Openflow_event() { }

    boost::shared_ptr<Openflow_datapath> dp;
    const struct ofp_header *oh;
};

class Openflow_datapath_join_event
    : public Event
{
public:
    Openflow_datapath_join_event(boost::shared_ptr<Openflow_datapath> dp_)
        : Event(static_get_name()), dp(dp_) { }

    static const Event_name static_get_name()
    {
        return "Openflow_datapath_join_event";
    }

    boost::shared_ptr<Openflow_datapath> dp;
};

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
};


class Openflow_request_controller_event
    : public Event
{
public:
    Openflow_request_controller_event(boost::shared_ptr<Openflow_datapath> dp_)
        : Event(static_get_name()), dp(dp_){ }

    static const Event_name static_get_name()
    {
        return "Openflow_request_controller_event";
    }
	
	boost::shared_ptr<Openflow_datapath> dp;
};


class Openflow_datapath_new_role_event
    : public Event
{
public:
    Openflow_datapath_new_role_event(boost::shared_ptr<Openflow_datapath> dp_, enum ofp_controller_role role_)
        : Event(static_get_name()), dp(dp_), role(role_){ }

    static const Event_name static_get_name()
    {
        return "Openflow_datapath_new_role_event";
    }

    boost::shared_ptr<Openflow_datapath> dp;
    enum ofp_controller_role role;
};

class Openflow_datapath_error_slave_role_event
    : public Event
{
public:
    Openflow_datapath_error_slave_role_event(boost::shared_ptr<Openflow_datapath> dp_)
        : Event(static_get_name()), dp(dp_) { }

    static const Event_name static_get_name()
    {
        return "Openflow_datapath_error_slave_role_event";
    }

    boost::shared_ptr<Openflow_datapath> dp;
};

class Packet_in_event : public Openflow_event
{
public:
    Packet_in_event(boost::shared_ptr<Openflow_datapath> dp_,
        std::string name_, const struct ofp_header *oh_):
        Openflow_event(dp_, name_, oh_)
    {}

    ~Packet_in_event() 
    {}
    struct flow flow;
    struct ofputil_packet_in pi;
};


} // namespace openflow
} // namespace vigil


std::string get_eventname_from_type(enum ofptype of_type);

std::string get_eventname_from_msg(const struct ofpbuf *msg);

#endif  // -- OFP_MSG_EVENT_HH
