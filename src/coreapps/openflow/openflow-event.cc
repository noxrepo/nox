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
#include "openflow-event.hh"
#include <string>
#include <sstream>

std::string get_eventname_from_type(enum ofptype of_type)
{
    switch(of_type)
    {
    case OFPTYPE_HELLO: { return std::string("Hello_event"); }
    case OFPTYPE_ERROR: { return std::string("Error_event"); }
    case OFPTYPE_ECHO_REQUEST: { return std::string("Echo_request_event"); }
    case OFPTYPE_ECHO_REPLY: { return std::string("Echo_reply_event");}
    case OFPTYPE_FEATURES_REPLY: { return std::string("Features_reply_event"); }
    case OFPTYPE_GET_CONFIG_REPLY: { return std::string("Get_config_reply_event"); }
    case OFPTYPE_FLOW_MOD: { return std::string("Flow_mod_event"); }
    case OFPTYPE_FLOW_REMOVED: { return std::string("Flow_removed_event"); }
    case OFPTYPE_PORT_STATUS: { return std::string("Port_status_event"); }
    case OFPTYPE_PACKET_IN: { return std::string("Packet_in_event"); }
    case OFPTYPE_BARRIER_REPLY: { return std::string("Barrier_reply_event"); }
    case OFPTYPE_QUEUE_GET_CONFIG_REPLY: { return std::string("Queue_get_config_reply_event"); }
    case OFPTYPE_ROLE_REPLY: { return std::string("Role_reply_event"); }
    case OFPTYPE_GET_ASYNC_REPLY: { return std::string("Async_reply_event"); }
    case OFPTYPE_PACKET_OUT: { return std::string("Packet_out_event"); }
    case OFPTYPE_PORT_STATS_REPLY: { return std::string("Port_stats_event"); }
    case OFPTYPE_PORT_DESC_STATS_REPLY: { return std::string("Port_desc_in_event"); }
    case OFPTYPE_FEATURES_REQUEST: { return std::string("Feature_request_event"); }
    case OFPTYPE_GET_CONFIG_REQUEST: { return std::string("Config_request_event"); }
    case OFPTYPE_SET_CONFIG:{ return std::string("Set_Config_event"); }
    case OFPTYPE_TENXT_GW_FDBS_REP:{return std::string("vgw_fdbs_rep");}
    default:
    {
        std::stringstream ss;
        ss << "ofptype-todo:" << of_type;
        return ss.str();}
    }
}

std::string get_eventname_from_msg(const struct ofpbuf *msg)
{
    struct ofp_header *oh = (struct ofp_header *)(msg->data);
    enum ofptype type;
    enum ofperr error;
    error = ofptype_decode(&type, oh);
    if (error)
    {
        return std::string("ofptype-decode-error");
    }

    return get_eventname_from_type(type);
}

