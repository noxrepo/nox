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
#include "openflow-1.0.hh"
#include "openflow-datapath-join-event.hh"
#include "openflow-datapath-leave-event.hh"
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
    VLOG_DBG(lg, "Compiled with OpenFlow 0x%x%x %s\n",
             (v1::OFP_VERSION >> 4) & 0x0f,
             (v1::OFP_VERSION & 0x0f),
             (v1::OFP_VERSION & 0x80) ? "(exp)" : "");
}

void
Openflow_manager::configure()
{
    register_default_events();
    register_default_handlers();
    register_handler<Shutdown_event>(
        boost::bind(&Openflow_manager::handle_shutdown, this, _1));

    v1::ofp_msg::init();
    v1::ofp_stats_request::init();
    v1::ofp_stats_reply::init();
    v1::ofp_vendor_stats_request::init();
    v1::ofp_vendor_stats_reply::init();
    v1::ofp_action::init();
    v1::ofp_action_vendor::init();
    v1::ofp_queue_prop::init();
    v1::ofp_vendor::init();
}

void
Openflow_manager::register_default_events()
{
    register_event<Openflow_datapath_join_event>();
    register_event<Openflow_datapath_leave_event>();

    // TODO: clean this up
    register_event("ofp_aggregate_stats_reply");
    register_event("ofp_aggregate_stats_request");
    register_event("ofp_barrier_reply");
    register_event("ofp_barrier_request");
    register_event("ofp_desc_stats_reply");
    register_event("ofp_desc_stats_request");
    register_event("ofp_echo_reply");
    register_event("ofp_echo_request");
    register_event("ofp_error_msg");
    register_event("ofp_features_reply");
    register_event("ofp_flow_mod");
    register_event("ofp_flow_removed");
    register_event("ofp_flow_stats_reply");
    register_event("ofp_flow_stats_request");
    register_event("ofp_get_config_reply");
    register_event("ofp_get_config_request");
    register_event("ofp_hello");
    register_event("ofp_packet_in");
    register_event("ofp_packet_out");
    register_event("ofp_port_mod");
    register_event("ofp_port_stats_reply");
    register_event("ofp_port_stats_request");
    register_event("ofp_port_status");
    register_event("ofp_queue_get_config_reply");
    register_event("ofp_queue_get_config_request");
    register_event("ofp_queue_stats_reply");
    register_event("ofp_queue_stats_request");
    register_event("ofp_set_config");
    register_event("ofp_stats_reply");
    register_event("ofp_stats_request");
    register_event("ofp_table_stats_reply");
    register_event("ofp_table_stats_request");
    register_event("ofp_vendor");
    register_event("ofp_vendor_stats_reply");
    register_event("ofp_vendor_stats_request");
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
    register_handler("ofp_echo_request",
                     boost::bind(&Openflow_manager::handle_echo_request, this, _1));
}

Disposition
Openflow_manager::handle_shutdown(const Event& e)
{
    //const Shutdown_event& se = assert_cast<const Shutdown_event&>(e);
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
    return CONTINUE;
}

Disposition
Openflow_manager::handle_echo_request(const Event& e)
{
    auto ofe = assert_cast<const Openflow_event&>(e);
    auto req = assert_cast<const v1::ofp_echo_request*>(ofe.msg);
    v1::ofp_echo_reply rep(*req);
    ofe.dp.send(&rep);
    return STOP;
}

REGISTER_COMPONENT(Simple_component_factory<Openflow_manager>, Openflow_manager);

} // namespace vigil
} // namespace vigil

