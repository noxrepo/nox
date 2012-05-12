/* Copyright (c) 2008 The Board of Trustees of The Leland Stanford
 * Junior University
 *
 * We are making the OpenFlow specification and associated documentation
 * (Software) available for public use and benefit with the expectation
 * that others will use, modify and enhance the Software and contribute
 * those enhancements back to the community. However, since we would
 * like to make the Software available for broadest use, with as few
 * restrictions as possible permission is hereby granted, free of
 * charge, to any person obtaining a copy of this Software to deal in
 * the Software under the copyrights without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * The name and trademarks of copyright holder(s) may NOT be used in
 * advertising or publicity pertaining to the Software or any
 * derivatives without specific, written prior permission.
 */

#include <openflow/openflow-1.0.hh>

namespace vigil
{
namespace openflow
{
namespace v1
{

ofp_msg::factory_map_t ofp_msg::factory_map;
ofp_stats_request::factory_map_t ofp_stats_request::factory_map;
ofp_stats_reply::factory_map_t ofp_stats_reply::factory_map;
ofp_vendor_stats_request::factory_map_t ofp_vendor_stats_request::factory_map;
ofp_vendor_stats_reply::factory_map_t ofp_vendor_stats_reply::factory_map;
ofp_action::factory_map_t ofp_action::factory_map;
ofp_action_vendor::factory_map_t ofp_action_vendor::factory_map;
ofp_queue_prop::factory_map_t ofp_queue_prop::factory_map;
ofp_vendor::factory_map_t ofp_vendor::factory_map;

} // namespace v1
} // namespace openflow
} // namespace vigil

/*
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_action_dl_addr)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_action_dl_dst)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_action_dl_src)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_action_enqueue)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_action_nw_addr)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_action_nw_dst)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_action_nw_src)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_action_nw_tos)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_action_output)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_action_strip_vlan)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_action_tp_dst)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_action_tp_port)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_action_tp_src)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_action_vendor)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_action_vlan_pcp)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_action_vlan_vid)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_aggregate_stats_reply)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_aggregate_stats_request)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_barrier_reply)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_barrier_request)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_desc_stats_reply)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_desc_stats_request)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_echo_reply)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_echo_request)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_error_msg)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_features_reply)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_features_request)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_flow_mod)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_flow_removed)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_flow_stats_reply)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_flow_stats_request)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_get_config_reply)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_get_config_request)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_hello)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_packet_in)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_packet_out)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_port_mod)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_port_stats_reply)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_port_stats_request)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_port_status)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_queue_get_config_reply)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_queue_get_config_request)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_queue_prop_min_rate)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_queue_prop_none)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_queue_stats_reply)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_queue_stats_request)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_set_config)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_stats_reply)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_stats_request)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_switch_config)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_table_stats_reply)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_table_stats_request)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_vendor)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_vendor_stats_reply)
BOOST_CLASS_EXPORT_IMPLEMENT(vigil::openflow::v1::ofp_vendor_stats_request)
*/
