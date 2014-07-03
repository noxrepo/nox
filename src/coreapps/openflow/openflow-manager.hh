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

#ifndef OPENFLOW_MANAGER_HH
#define OPENFLOW_MANAGER_HH 1

#include <set>
#include <boost/asio/streambuf.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/unordered_map.hpp>

#include "component.hh"
#include "of_nox.hh"
#include "openflow-datapath.hh"
#include "netinet++/datapathid.hh"

namespace vigil
{
namespace openflow
{

class Openflow_datapath;

/* Openflow component */
class Openflow_manager : public Component
{
public:
    Openflow_manager(const Component_context* ctxt);
    void configure();
    bool get_dp_from_dpid(const datapathid &dpid, boost::shared_ptr<Openflow_datapath> &dp);

private:
    typedef boost::unordered_map<datapathid, boost::shared_ptr<Openflow_datapath> >
    Datapath_map;
    typedef std::set<boost::shared_ptr<Openflow_datapath> > Datapath_set;

    Datapath_map connected_dps;
    Datapath_set connecting_dps;
    boost::mutex dp_mutex;

    void register_default_events();
    void register_default_handlers();

    Disposition handle_shutdown(const Event&);
    Disposition handle_new_connection(const Event&);
    Disposition handle_datapath_join(const Event&);
    Disposition handle_datapath_leave(const Event&);
    Disposition handle_echo_request(const Event&);
    Disposition handle_role_reply(const Event&);
    Disposition handle_vendor(const Event&);
    Disposition handle_error(const Event&);
};

} // namespace openflow
} // namespace vigil

#endif
