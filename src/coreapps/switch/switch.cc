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

#include <cstring>
#include <netinet/in.h>
#include <stdexcept>
#include <stdint.h>
#include <string>

#include <boost/bind.hpp>
#include <boost/foreach.hpp>

#include <tbb/concurrent_hash_map.h>

#include "assert.hh"
#include "component.hh"
#include "vlog.hh"

#include "netinet++/datapathid.hh"
#include "netinet++/ethernetaddr.hh"
#include "netinet++/ethernet.hh"

#include "openflow/openflow-event.hh"
#include "openflow/openflow-datapath-join-event.hh"
#include "openflow/openflow-datapath-leave-event.hh"

using namespace vigil;
using namespace openflow;

namespace
{

Vlog_module lg("switch");

class Switch
    : public Component
{
public:
    Switch(const Component_context* c)
        : Component(c)
    {
        setup_flows = true; // default value
    }

    void configure();

    void install() {}

    Disposition handle_datapath_join(const Event&);
    Disposition handle_datapath_leave(const Event&);
    Disposition handle_packet_in(const Event&);

private:
    struct datapath_hasher {
        static size_t hash(const datapathid& o) {
            return boost::hash_value(o.as_host());
        }
        static bool equal(const datapathid& o1, const datapathid& o2)
        {
            return o1 == o2;
        }
    };
    typedef boost::unordered_map<ethernetaddr, int> mac_table;
    typedef tbb::concurrent_hash_map<datapathid, mac_table, datapath_hasher> mac_table_map;

    mac_table_map mac_tables;

    /* Set up a flow when we know the destination of a packet?  This should
     * ordinarily be true; it is only usefully false for debugging purposes. */
    bool setup_flows;
};

inline void
Switch::configure()
{
    if (ctxt->has("args")) {
        BOOST_FOREACH (const std::string& arg, ctxt->get_config_list("args"))
        {
            if (arg == "noflow")
            {
                setup_flows = false;
            }
            else
            {
                VLOG_WARN(lg, "argument \"%s\" not supported", arg.c_str());
            }
        }
    }
    register_handler("Openflow_datapath_join_event", (boost::bind(&Switch::handle_datapath_join, this, _1)));
    register_handler("Openflow_datapath_leave_event", (boost::bind(&Switch::handle_datapath_leave, this, _1)));
    register_handler("ofp_packet_in", (boost::bind(&Switch::handle_packet_in, this, _1)));
}

inline Disposition
Switch::handle_datapath_join(const Event& e)
{
    auto& dpje = assert_cast<const Openflow_datapath_join_event&>(e);
    mac_tables.insert(std::make_pair(dpje.dp->id(), mac_table()));
    return CONTINUE;
}

inline Disposition
Switch::handle_datapath_leave(const Event& e)
{
    auto& dple = assert_cast<const Openflow_datapath_leave_event&>(e);
    mac_tables.erase(dple.dp->id());
    return CONTINUE;
}

inline Disposition
Switch::handle_packet_in(const Event& e)
{
    auto ofe = assert_cast<const Openflow_event&>(e);
    auto& dp = ofe.dp;
    auto pi = *(assert_cast<const v1::ofp_packet_in*>(ofe.msg));
    int out_port = -1;        // Flood by default

    v1::ofp_match flow;
    flow.from_packet(pi.in_port(), pi.packet());

    // Drop all LLDP packets
    if (flow.dl_type() == ethernet::LLDP)
    {
        return CONTINUE;
    }

    mac_table_map::accessor accessor;
    mac_tables.find(accessor, dp.id());
    auto mac_table = accessor->second;

    // Learn the source MAC
    if (!flow.dl_src().is_multicast())
    {
        mac_table[flow.dl_src()] = pi.in_port();
    }

    if (!flow.dl_dst().is_multicast())
    {
        auto it = mac_table.find(flow.dl_dst());
        if (it != mac_table.end())
            out_port = it->second;
    }

    // Set up a flow if the output port is known
    if (setup_flows && out_port != -1)
    {
        auto& fm = v1::ofp_flow_mod().match(flow).buffer_id(pi.buffer_id())
                   .cookie(0).command(v1::ofp_flow_mod::OFPFC_ADD).idle_timeout(5)
                   .hard_timeout(v1::OFP_FLOW_PERMANENT)
                   .priority(v1::OFP_DEFAULT_PRIORITY);
        auto& ao = v1::ofp_action_output().port(out_port);
        fm.add_action(&ao);
        dp.send(&fm);
    }

    // Send out packet if necessary
    if (!setup_flows || out_port == -1 || pi.buffer_id() == UINT32_MAX)
    {
        if (out_port == -1)
            out_port = v1::ofp_phy_port::OFPP_FLOOD;

        auto& po = v1::ofp_packet_out().in_port(pi.in_port());
        auto& ao = v1::ofp_action_output().port(out_port);
        po.add_action(&ao);

        if (pi.buffer_id() == UINT32_MAX)
        {
            if (pi.total_len() != boost::asio::buffer_size(pi.packet()))
            {
                /* Control path didn't buffer the packet and didn't send us
                 * the whole thing--what gives? */
                VLOG_DBG(lg, "total_len=%u data_len=%zu\n",
                         pi.total_len(), boost::asio::buffer_size(pi.packet()));
                return CONTINUE;
            }
            po.packet(pi.packet());
        }
        else
        {
            po.buffer_id(pi.buffer_id());
        }
        dp.send(&po);
    }
    return CONTINUE;
}

REGISTER_COMPONENT(Simple_component_factory<Switch>, Switch);

} // unnamed namespace
