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

#include "openflow-manager.hh"

#include <config.h>
#include <inttypes.h>
#include <algorithm>
#include <utility>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/make_shared.hpp>
#include <boost/pool/object_pool.hpp>
#include <boost/ref.hpp>
#include <boost/thread/locks.hpp>
#include <boost/timer.hpp>

#include "assert.hh"
#include "openflow-event.hh"
#include "new-connection-event.hh"
#include "shutdown-event.hh"
#include "vlog.hh"

namespace vigil
{
namespace openflow
{

static Vlog_module lg("openflow-manager");

Openflow_manager::Openflow_manager(const Component_context* ctxt)
    : Component(ctxt)
{
}

void
Openflow_manager::configure()
{
    register_default_events();
    register_default_handlers();
    register_handler<Shutdown_event>(
        boost::bind(&Openflow_manager::handle_shutdown, this, _1));
}

bool Openflow_manager::get_dp_from_dpid(const datapathid &dpid, boost::shared_ptr<Openflow_datapath> &dp)
{
    Datapath_map::iterator it = connected_dps.find(dpid);
    if (it != connected_dps.end())
    {
        dp = it->second;
        return true;
    }
    else
    {
        return false;
    }
}

void
Openflow_manager::register_default_events()
{
    register_event<Openflow_datapath_join_event>();
    register_event<Openflow_datapath_leave_event>();
    register_event<Openflow_datapath_new_role_event>();
    register_event<Openflow_datapath_error_slave_role_event>();
    register_event<Openflow_request_controller_event>();
    register_event(get_eventname_from_type(OFPTYPE_HELLO));
    register_event(get_eventname_from_type(OFPTYPE_ERROR));
    register_event(get_eventname_from_type(OFPTYPE_ECHO_REQUEST));
    register_event(get_eventname_from_type(OFPTYPE_ECHO_REPLY));
    register_event(get_eventname_from_type(OFPTYPE_FEATURES_REPLY));
    register_event(get_eventname_from_type(OFPTYPE_SET_CONFIG));
    register_event(get_eventname_from_type(OFPTYPE_GET_CONFIG_REPLY));
    register_event(get_eventname_from_type(OFPTYPE_PACKET_IN));
    register_event(get_eventname_from_type(OFPTYPE_FLOW_REMOVED));
    register_event(get_eventname_from_type(OFPTYPE_PORT_DESC_STATS_REPLY));
    register_event(get_eventname_from_type(OFPTYPE_PORT_STATUS));
    register_event(get_eventname_from_type(OFPTYPE_ROLE_REPLY));
    register_event(get_eventname_from_type(OFPTYPE_GET_ASYNC_REPLY));
    register_event(get_eventname_from_type(OFPTYPE_BARRIER_REPLY));
    register_event(get_eventname_from_type(OFPTYPE_QUEUE_GET_CONFIG_REPLY));
    register_event(get_eventname_from_type(OFPTYPE_TENXT_GW_FDBS_REP));
}

void
Openflow_manager::register_default_handlers()
{
    register_handler<New_connection_event>(
        boost::bind(&Openflow_manager::handle_new_connection, this, _1));
    register_handler<Openflow_datapath_join_event>(
        boost::bind(&Openflow_manager::handle_datapath_join, this, _1));
    register_handler<Openflow_datapath_leave_event>(
        boost::bind(&Openflow_manager::handle_datapath_leave, this, _1));
    register_handler(get_eventname_from_type(OFPTYPE_ECHO_REQUEST),
        boost::bind(&Openflow_manager::handle_echo_request, this, _1));
    register_handler(get_eventname_from_type(OFPTYPE_ROLE_REPLY),
        boost::bind(&Openflow_manager::handle_role_reply, this, _1));
    register_handler(get_eventname_from_type(OFPTYPE_ERROR),
        boost::bind(&Openflow_manager::handle_error, this, _1));
}

Disposition
Openflow_manager::handle_shutdown(const Event& e)
{
    //const Shutdown_event& se = assert_cast<const Shutdown_event&>(e);
    boost::lock_guard<boost::mutex> lock(dp_mutex);
    BOOST_FOREACH(auto conn, connecting_dps)
    {
        conn->close();
    }
    BOOST_FOREACH(auto conn, connected_dps)
    {
        conn.second->close();
    }
    return CONTINUE;
}

Disposition
Openflow_manager::handle_new_connection(const Event& e)
{
    auto nce = assert_cast<const New_connection_event&>(e);
    boost::shared_ptr<Openflow_datapath> dp(new Openflow_datapath(*this));
    dp->set_connection(nce.connection);
    boost::lock_guard<boost::mutex> lock(dp_mutex);
    connecting_dps.insert(dp);
    return CONTINUE;
}

Disposition
Openflow_manager::handle_datapath_join(const Event& e)
{
    auto dje = assert_cast<const Openflow_datapath_join_event&>(e);
    boost::lock_guard<boost::mutex> lock(dp_mutex);
    connecting_dps.erase(dje.dp);
    connected_dps[dje.dp->id()] = dje.dp;
    return CONTINUE;
}

Disposition
Openflow_manager::handle_datapath_leave(const Event& e)
{
    auto dle = assert_cast<const Openflow_datapath_leave_event&>(e);
    boost::lock_guard<boost::mutex> lock(dp_mutex);
    connected_dps.erase(dle.dp->id());
    connecting_dps.erase(dle.dp);
    return CONTINUE;
}

Disposition
Openflow_manager::handle_echo_request(const Event& e)
{
    auto ofe = assert_cast<const Openflow_event&>(e);
    ofe.dp->send_echo_reply(e);
    return STOP;
}

Disposition Openflow_manager::handle_role_reply(const Event &e)
{
    auto ofe = assert_cast<const Openflow_event &>(e);
    auto &dp = ofe.dp;

	enum ofp_controller_role role;
    struct ofputil_role_request rr;
    enum ofperr error;

    error = ofputil_decode_role_message(ofe.oh, &rr);
    if (error)
    {
        lg.err("decode role msg failed");
        return CONTINUE;
    }

    role = (ofp_controller_role)rr.role;
    if(role != OFPCR_ROLE_NOCHANGE)
    {
        dp->set_role(role);
        lg.info("dispatch new role event");
        Openflow_datapath_new_role_event odnre(dp, role);
        dispatch(odnre);
    }

    return CONTINUE;
}


Disposition
Openflow_manager::handle_vendor(const Event& e)
{
    return CONTINUE;
}

Disposition
Openflow_manager::handle_error(const Event &e)
{
    auto ofe = assert_cast<const Openflow_event &>(e);
	struct ofpbuf payload;
    enum ofperr error;

    error = ofperr_decode_msg(ofe.oh, &payload);
    if (error == OFPERR_OFPBRC_IS_SLAVE)
    {
        Openflow_datapath_error_slave_role_event odesre(ofe.dp);
        dispatch(odesre);
    }

	return CONTINUE;
}


REGISTER_COMPONENT(Simple_component_factory<Openflow_manager>, Openflow_manager);

} // namespace vigil
} // namespace vigil

