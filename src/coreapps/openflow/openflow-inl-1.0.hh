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
 * the following conditions
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

#ifndef OPENFLOW_OF1_INL_H
#define OPENFLOW_OF1_INL_H 1

#include <arpa/inet.h>

#include <boost/bind.hpp>
#include <boost/functional/factory.hpp>
#include <boost/functional/value_factory.hpp>
#include <boost/serialization/array.hpp>
#include <boost/serialization/binary_object.hpp>
#include <boost/serialization/export.hpp>
#include <boost/serialization/serialization.hpp>
#include <boost/type_traits.hpp>
#include <boost/utility/enable_if.hpp>

#include "packets.h"

namespace vigil
{
namespace openflow
{
namespace v1
{

namespace bs = boost::serialization;

// Factory
template<typename T>
struct has_factory
{
    template <typename C> static char test(typeof(&C::factory()));
    template <typename C> static long test(...);

public:
    enum { value = sizeof(test<T>(0)) == sizeof(char) };
};

template <typename Message>
class serialize_visitor : public boost::static_visitor<void>
{
public:
    serialize_visitor(Message& msg) : msg(msg) {}
    template <typename Archive>
    void operator()(Archive& ar) const
    {
        //ar & msg;
        msg.serialize(ar, 0);
    }

private:
    Message& msg;
};

template<class Derived, class Base, class Enable = void>
struct construct_and_load
{
    void
    operator()(ofp_archive_type ar, Base* mem, Base& b)
    {
        if (mem != NULL) {
            ::new(mem)Derived(b);
            Derived* d = reinterpret_cast<Derived*>(mem);
            boost::apply_visitor(serialize_visitor<Derived>(*d), ar);
        } else {
            Derived& d = reinterpret_cast<Derived&>(b);
            boost::apply_visitor(serialize_visitor<Derived>(d), ar);
        }
    }
};

template<class Derived, class Base>
struct construct_and_load
        <Derived, Base, typename boost::enable_if<has_factory<Derived> >::type>
{
    void
    operator()(ofp_archive_type ar, Base* mem, Base& b)
    {
        if (mem != NULL) {
            Derived d(b);
            d.factory(ar, reinterpret_cast<Derived*>(mem));
        } else {
            Derived& d = reinterpret_cast<Derived&>(b);
            d.factory(ar, NULL);
        }
    }
};

#define REGISTER_FACTORY(T, ID); \
    register_factory(ID, construct_and_load<T, ofp_msg>())

inline void ofp_msg::init()
{
    REGISTER_FACTORY(ofp_hello, OFPT_HELLO);
    REGISTER_FACTORY(ofp_error_msg, OFPT_ERROR);
    REGISTER_FACTORY(ofp_echo_request, OFPT_ECHO_REQUEST);
    REGISTER_FACTORY(ofp_echo_reply, OFPT_ECHO_REPLY);
    REGISTER_FACTORY(ofp_vendor, OFPT_VENDOR);
    REGISTER_FACTORY(ofp_features_request, OFPT_FEATURES_REQUEST);
    REGISTER_FACTORY(ofp_features_reply, OFPT_FEATURES_REPLY);
    REGISTER_FACTORY(ofp_get_config_request, OFPT_GET_CONFIG_REQUEST);
    REGISTER_FACTORY(ofp_get_config_reply, OFPT_GET_CONFIG_REPLY);
    REGISTER_FACTORY(ofp_set_config, OFPT_SET_CONFIG);
    REGISTER_FACTORY(ofp_packet_in, OFPT_PACKET_IN);
    REGISTER_FACTORY(ofp_flow_removed, OFPT_FLOW_REMOVED);
    REGISTER_FACTORY(ofp_port_status, OFPT_PORT_STATUS);
    REGISTER_FACTORY(ofp_packet_out, OFPT_PACKET_OUT);
    REGISTER_FACTORY(ofp_flow_mod, OFPT_FLOW_MOD);
    REGISTER_FACTORY(ofp_port_mod, OFPT_PORT_MOD);
    REGISTER_FACTORY(ofp_stats_request, OFPT_STATS_REQUEST);
    REGISTER_FACTORY(ofp_stats_reply, OFPT_STATS_REPLY);
    REGISTER_FACTORY(ofp_barrier_request, OFPT_BARRIER_REQUEST);
    REGISTER_FACTORY(ofp_barrier_reply, OFPT_BARRIER_REPLY);
    REGISTER_FACTORY(ofp_queue_get_config_request, OFPT_QUEUE_GET_CONFIG_REQUEST);
    REGISTER_FACTORY(ofp_queue_get_config_reply, OFPT_QUEUE_GET_CONFIG_REPLY);
}

#undef REGISTER_FACTORY
#define REGISTER_FACTORY(T, ID); \
    register_factory(ID, construct_and_load<T, ofp_stats_request>())

inline void ofp_stats_request::init()
{
    REGISTER_FACTORY(ofp_desc_stats_request, OFPST_DESC);
    REGISTER_FACTORY(ofp_flow_stats_request, OFPST_FLOW);
    REGISTER_FACTORY(ofp_aggregate_stats_request, OFPST_AGGREGATE);
    REGISTER_FACTORY(ofp_table_stats_request, OFPST_TABLE);
    REGISTER_FACTORY(ofp_port_stats_request, OFPST_PORT);
    REGISTER_FACTORY(ofp_queue_stats_request, OFPST_QUEUE);
    REGISTER_FACTORY(ofp_vendor_stats_request, OFPST_VENDOR);
}

#undef REGISTER_FACTORY
#define REGISTER_FACTORY(T, ID); \
    register_factory(ID, construct_and_load<T, ofp_stats_reply>())

inline void ofp_stats_reply::init()
{
    REGISTER_FACTORY(ofp_desc_stats_reply, OFPST_DESC);
    REGISTER_FACTORY(ofp_flow_stats_reply, OFPST_FLOW);
    REGISTER_FACTORY(ofp_aggregate_stats_reply, OFPST_AGGREGATE);
    REGISTER_FACTORY(ofp_table_stats_reply, OFPST_TABLE);
    REGISTER_FACTORY(ofp_port_stats_reply, OFPST_PORT);
    REGISTER_FACTORY(ofp_queue_stats_reply, OFPST_QUEUE);
    REGISTER_FACTORY(ofp_vendor_stats_reply, OFPST_VENDOR);
}

#undef REGISTER_FACTORY
#define REGISTER_FACTORY(T, ID); \
    register_factory(ID, construct_and_load<T, ofp_vendor_stats_request>())

inline void ofp_vendor_stats_request::init()
{
}

#undef REGISTER_FACTORY
#define REGISTER_FACTORY(T, ID); \
    register_factory(ID, construct_and_load<T, ofp_vendor_stats_reply>())

inline void ofp_vendor_stats_reply::init()
{
}

#undef REGISTER_FACTORY
#define REGISTER_FACTORY(T, ID); \
    register_factory(ID, construct_and_load<T, ofp_action>())

inline void ofp_action::init()
{
    REGISTER_FACTORY(ofp_action_output, OFPAT_OUTPUT);
    REGISTER_FACTORY(ofp_action_vlan_vid, OFPAT_SET_VLAN_VID);
    REGISTER_FACTORY(ofp_action_vlan_pcp, OFPAT_SET_VLAN_PCP);
    REGISTER_FACTORY(ofp_action_strip_vlan, OFPAT_STRIP_VLAN);
    REGISTER_FACTORY(ofp_action_dl_addr, OFPAT_SET_DL_SRC);
    REGISTER_FACTORY(ofp_action_dl_addr, OFPAT_SET_DL_DST);
    REGISTER_FACTORY(ofp_action_nw_addr, OFPAT_SET_NW_SRC);
    REGISTER_FACTORY(ofp_action_nw_addr, OFPAT_SET_NW_DST);
    REGISTER_FACTORY(ofp_action_nw_tos, OFPAT_SET_NW_TOS);
    REGISTER_FACTORY(ofp_action_tp_port, OFPAT_SET_TP_SRC);
    REGISTER_FACTORY(ofp_action_tp_port, OFPAT_SET_TP_DST);
    REGISTER_FACTORY(ofp_action_enqueue, OFPAT_ENQUEUE);
    REGISTER_FACTORY(ofp_action_vendor, OFPAT_VENDOR);
}

#undef REGISTER_FACTORY
#define REGISTER_FACTORY(T, ID); \
    register_factory(ID, construct_and_load<T, ofp_action_vendor>())

inline void ofp_action_vendor::init()
{
}

#undef REGISTER_FACTORY
#define REGISTER_FACTORY(T, ID); \
    register_factory(ID, construct_and_load<T, ofp_queue_prop>())

inline void ofp_queue_prop::init()
{
    REGISTER_FACTORY(ofp_queue_prop_none, OFPQT_NONE);
    REGISTER_FACTORY(ofp_queue_prop_min_rate, OFPQT_MIN_RATE);
}

#undef REGISTER_FACTORY
#define REGISTER_FACTORY(T, ID); \
    register_factory(ID, construct_and_load<T, ofp_vendor>())

inline void ofp_vendor::init()
{
}

#undef REGISTER_FACTORY

/*
inline ofp_msg::factory_t ofp_msg::construct(const uint8_t* buf)
{
    const uint8_t& type = buf[offsetof(ofp_msg, type_)];
    const uint16_t* len = reinterpret_cast<const uint16_t*>(buf + offsetof(ofp_msg, length_));
    const uint16_t length = ntohs(*len);
    return NULL;
}
*/
inline void ofp_msg::factory(ofp_archive_type ar, ofp_msg* mem)
{
    return factory_map[type()](ar, mem, *this);
}

inline void ofp_msg::register_factory(uint8_t t, ofp_msg::factory_t f)
{
    factory_map[t] = f;
}

inline void ofp_stats_request::factory(ofp_archive_type ar, ofp_stats_request* mem)
{
    return factory_map[type()](ar, mem, *this);
}

inline void ofp_stats_request::register_factory(uint16_t t,
                                                ofp_stats_request::factory_t f)
{
    factory_map[t] = f;
}

inline void ofp_stats_reply::factory(ofp_archive_type ar, ofp_stats_reply* mem)
{
    return factory_map[type()](ar, mem, *this);
}

inline void ofp_stats_reply::register_factory(uint16_t t,
                                              ofp_stats_reply::factory_t f)
{
    factory_map[t] = f;
}

inline void ofp_vendor_stats_request::factory(ofp_archive_type ar, ofp_vendor_stats_request* mem)
{
    return factory_map[vendor()](ar, mem, *this);
}

inline void ofp_vendor_stats_request::register_factory(uint32_t t,
                                                       ofp_vendor_stats_request::factory_t f)
{
    factory_map[t] = f;
}

inline void ofp_vendor_stats_reply::factory(ofp_archive_type ar, ofp_vendor_stats_reply* mem)
{
    return factory_map[vendor()](ar, mem, *this);
}

inline void ofp_vendor_stats_reply::register_factory(uint32_t t,
                                                     ofp_vendor_stats_reply::factory_t f)
{
    factory_map[t] = f;
}
inline void ofp_action::factory(ofp_archive_type ar, ofp_action* mem)
{
    return factory_map[type()](ar, mem, *this);
}

inline void ofp_action::register_factory(uint16_t t, ofp_action::factory_t f)
{
    factory_map[t] = f;
}

inline void ofp_action_vendor::factory(ofp_archive_type ar, ofp_action_vendor* mem)
{
    return factory_map[vendor()](ar, mem, *this);
}

inline void ofp_action_vendor::register_factory(uint32_t t,
                                                ofp_action_vendor::factory_t f)
{
    factory_map[t] = f;
}

inline void ofp_queue_prop::factory(ofp_archive_type ar, ofp_queue_prop* mem)
{
    return factory_map[property()](ar, mem, *this);
}

inline void ofp_queue_prop::register_factory(uint16_t t,
                                             ofp_queue_prop::factory_t f)
{
    factory_map[t] = f;
}

inline void ofp_vendor::factory(ofp_archive_type ar, ofp_vendor* mem)
{
    return factory_map[vendor()](ar, mem, *this);
}

inline void ofp_vendor::register_factory(uint32_t t, ofp_vendor::factory_t f)
{
    factory_map[t] = f;
}

// Serialization
template<class Archive>
inline void ofp_packet_out::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_msg>(*this);
    /*
    ar & bs::make_binary_object(&buffer_id_, 8);
    */
    ar& buffer_id_;
    ar& in_port_;
    ar& actions_len_;
    if (Archive::is_loading::value)
        actions_.length(actions_len_);
    ar& actions_;

    uint8_t size = length() - min_bytes() - actions_len_;
    const uint8_t* packet = boost::asio::buffer_cast<const uint8_t*>(packet_buf_);
    ar& bs::make_binary_object(const_cast<uint8_t*>(packet), size);
    packet_buf_ = boost::asio::buffer(packet, size);
}

template<class Archive>
inline void ofp_action_nw_addr::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_action>(*this);
    ar& nw_addr_;
}

template<class Archive>
inline void ofp_queue_get_config_request::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_msg>(*this);
    ar& port_;
    ar& bs::make_binary_object(pad_, sizeof pad_);
}

inline uint32_t ofp_phy_port::speed()
{
    if (curr_ & (OFPPF_10MB_HD | OFPPF_10MB_FD))
    {
        return 10;
    }
    else if (curr_ & (OFPPF_100MB_HD | OFPPF_100MB_FD))
    {
        return 100;
    }
    else if (curr_ & (OFPPF_1GB_HD | OFPPF_1GB_FD))
    {
        return 1000;
    }
    else if (curr_ & OFPPF_10GB_FD)
    {
        return 10000;
    }
    return 0;
}

template<class Archive>
inline void ofp_phy_port::serialize(Archive& ar, const unsigned int)
{
    ar& port_no_;
    ar& hw_addr_;
    ar& bs::make_binary_object(name_, sizeof name_);
    ar& config_;
    ar& state_;
    ar& curr_;
    ar& advertised_;
    ar& supported_;
    ar& peer_;
}

template<class Archive>
inline void ofp_action_dl_addr::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_action>(*this);
    ar& dl_addr_;
    ar& bs::make_binary_object(pad_, sizeof pad_);
}

template<class Archive>
inline void ofp_switch_config::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_msg>(*this);
    ar& flags_;
    ar& miss_send_len_;
}

template<class Archive>
inline void ofp_action_vlan_pcp::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_action>(*this);
    ar& vlan_pcp_;
    ar& bs::make_binary_object(pad_, sizeof pad_);
}

template<class Archive>
inline void ofp_queue_stats::serialize(Archive& ar, const unsigned int)
{
    ar& port_no_;
    ar& bs::make_binary_object(pad_, sizeof pad_);
    ar& queue_id_;
    ar& tx_bytes_;
    ar& tx_packets_;
    ar& tx_errors_;
}

template<class Archive>
inline void ofp_flow_stats::serialize(Archive& ar, const unsigned int)
{
    ar& length_;
    ar& table_id_;
    ar& pad_;
    ar& match_;
    ar& duration_sec_;
    ar& duration_nsec_;
    ar& priority_;
    ar& idle_timeout_;
    ar& hard_timeout_;
    ar& bs::make_binary_object(pad2_, sizeof pad2_);
    ar& cookie_;
    ar& packet_count_;
    ar& byte_count_;
    actions_.length(length() - min_bytes());
    ar& actions_;
}

template<class Archive>
inline void ofp_features_reply::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_msg>(*this);
    ar& datapath_id_;
    ar& n_buffers_;
    ar& n_tables_;
    ar& bs::make_binary_object(pad_, sizeof pad_);
    ar& capabilities_;
    ar& actions_;
    n_ports_ = (length() - min_bytes()) / OFP_PHY_PORT_BYTES;
    ar& bs::make_array(ports_, n_ports_);
}

template<class Archive>
inline void ofp_action_nw_tos::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_action>(*this);
    ar& nw_tos_;
    ar& bs::make_binary_object(pad_, sizeof pad_);
}

template<class Archive>
inline void ofp_action::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value || type() == OFPAT_INVALID)
    {
        ar& type_;
        ar& len_;
    }
}

template<class Archive>
inline void ofp_action_list::serialize(Archive& ar, const unsigned int)
{
    // For loading we rely on length to infer the size
    std::size_t i = 0;
    std::size_t byte_count = length_;
    if (Archive::is_loading::value) {
        while (byte_count > 0) {
            ofp_action action;
            ar& action;
            list_[i] = reinterpret_cast<ofp_action*>(storage_[i]);
            action.factory(ar, const_cast<ofp_action*>(list_[i]));
            byte_count -= list_[i++]->len();
        }
    } else {
        while (byte_count > 0) {
            byte_count -= list_[i]->len();
            const_cast<ofp_action*>(list_[i++])->factory(ar, NULL);
        }
    }
}

template<class Archive>
inline void ofp_packet_queue::serialize(Archive& ar, const unsigned int)
{
    ar& queue_id_;
    ar& len_;
    ar& bs::make_binary_object(pad_, sizeof pad_);
    ar& properties_;
}

template<class Archive>
inline void ofp_vendor::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_msg>(*this);
    if (Archive::is_saving::value || vendor() == OFPVT_INVALID)
        ar& vendor_;
}

template<class Archive>
inline void ofp_error_msg::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_msg>(*this);
    ar& type_;
    ar& code_;
    data_size_ = length() - min_bytes();
    ar& bs::make_binary_object(data_, data_size_);
}

template<class Archive>
inline void ofp_match::serialize(Archive& ar, const unsigned int)
{
    ar& wildcards_;
    ar& in_port_;
    ar& dl_src_;
    ar& dl_dst_;
    ar& dl_vlan_;
    ar& dl_vlan_pcp_;
    ar& bs::make_binary_object(pad1_, sizeof pad1_);
    ar& dl_type_;
    ar& nw_tos_;
    ar& nw_proto_;
    ar& bs::make_binary_object(pad2_, sizeof pad2_);
    ar& nw_src_;
    ar& nw_dst_;
    ar& tp_src_;
    ar& tp_dst_;
}

template<class Archive>
inline void ofp_queue_get_config_reply::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_msg>(*this);
    ar& port_;
    ar& bs::make_binary_object(pad_, sizeof pad_);
    n_queues_ = (length() - OFP_QUEUE_GET_CONFIG_REPLY_BYTES) / OFP_PACKET_QUEUE_BYTES;
    ar& bs::make_array(queues_, n_queues_);
}

template<class Archive>
inline void ofp_stats::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_msg>(*this);
    if (Archive::is_saving::value || type() == OFPST_INVALID)
    {
        ar& type_;
        ar& flags_;
    }
}

template<class Archive>
inline void ofp_stats_request::serialize(Archive& ar, const unsigned int)
{
    ar& bs::base_object<ofp_stats>(*this);
}

template<class Archive>
inline void ofp_stats_reply::serialize(Archive& ar, const unsigned int)
{
    ar& bs::base_object<ofp_stats>(*this);
}

template<class Archive>
inline void ofp_flow_stats_request::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_stats_request>(*this);
    ar& match_;
    ar& table_id_;
    ar& pad_;
    ar& out_port_;
}

template<class Archive>
inline void ofp_aggregate_stats_request::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_stats_request>(*this);
    ar& match_;
    ar& table_id_;
    ar& pad_;
    ar& out_port_;
}

template<class Archive>
inline void ofp_port_stats_request::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_stats_request>(*this);
    ar& port_no_;
    ar& bs::make_binary_object(pad_, sizeof pad_);
}

template<class Archive>
inline void ofp_queue_stats_request::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_stats_request>(*this);
    ar& port_no_;
    ar& bs::make_binary_object(pad_, sizeof pad_);
    ar& queue_id_;
}

template<class Archive>
inline void ofp_vendor_stats_request::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_stats_request>(*this);
    if (Archive::is_saving::value || vendor() == OFPVT_INVALID)
        ar& vendor_;
}

template<class Archive>
inline void ofp_desc_stats_reply::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_stats_reply>(*this);
    ar& bs::make_binary_object(mfr_desc_, sizeof mfr_desc_);
    ar& bs::make_binary_object(hw_desc_, sizeof hw_desc_);
    ar& bs::make_binary_object(sw_desc_, sizeof sw_desc_);
    ar& bs::make_binary_object(serial_num_, sizeof serial_num_);
    ar& bs::make_binary_object(dp_desc_, sizeof dp_desc_);
}

template<class Archive>
inline void ofp_flow_stats_reply::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_stats_reply>(*this);
    // TODO: fix vector
    //ar & bs::make_array(&v_[0], v_.size());
}

template<class Archive>
inline void ofp_aggregate_stats_reply::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_stats_reply>(*this);
    ar& packet_count_;
    ar& byte_count_;
    ar& flow_count_;
    ar& bs::make_binary_object(pad_, sizeof pad_);
}

template<class Archive>
inline void ofp_table_stats_reply::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_stats_reply>(*this);
    //ar & bs::make_array(&v_[0], v_.size());
}

template<class Archive>
inline void ofp_port_stats_reply::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_stats_reply>(*this);
    //ar & bs::make_array(&v_[0], v_.size());
}

template<class Archive>
inline void ofp_queue_stats_reply::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_stats_reply>(*this);
    //ar & bs::make_array(&v_[0], v_.size());
}

template<class Archive>
inline void ofp_vendor_stats_reply::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_stats_reply>(*this);
    if (Archive::is_saving::value || vendor() == OFPVT_INVALID)
        ar& vendor_;
}

template<class Archive>
inline void ofp_msg::serialize(Archive& ar, const unsigned int)
{
    // TODO: any better way?
    // deserialize only if object is not a subclass
    if (Archive::is_saving::value || type() == OFPT_INVALID)
    {
        ar& version_;
        ar& type_;
        ar& length_;
        ar& xid_;
        // TODO: trial
        /*

        if (Archive::is_saving::value) {
            length_ = ntohs(length_);
            xid_ = htonl(xid_);
        }
        ar & bs::make_binary_object(&version_, min_bytes());
        length_ = ntohs(length_);
        xid_ = htonl(xid_);
        ar & bs::make_binary_object(&version_, min_bytes());
        */
    }
}

template<class Archive>
inline void ofp_packet_in::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_msg>(*this);
    /*
    ar & bs::make_binary_object(&buffer_id_, length() - 8);
    uint8_t size = length() - min_bytes();
    packet_buf_ = boost::asio::buffer(
            const_cast<uint8_t*>(
                boost::asio::buffer_cast<const uint8_t*>(packet_buf_)),
            size);
    */
    ar& buffer_id_;
    ar& total_len_;
    ar& in_port_;
    ar& reason_;
    ar& pad_;
    uint8_t size = length() - min_bytes();
    uint8_t* packet = const_cast<uint8_t*>(boost::asio::buffer_cast<const uint8_t*>(packet_buf_));
    ar& bs::make_binary_object(packet, size);
    packet_buf_ = boost::asio::buffer(packet, size);
}

template<class Archive>
inline void ofp_port_status::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_msg>(*this);
    ar& reason_;
    ar& bs::make_binary_object(pad_, sizeof pad_);
    ar& desc_;
}

template<class Archive>
inline void ofp_flow_mod::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_msg>(*this);
    ar& match_;
    ar& cookie_;
    ar& command_;
    ar& idle_timeout_;
    ar& hard_timeout_;
    ar& priority_;
    ar& buffer_id_;
    ar& out_port_;
    ar& flags_;
    actions_.length(length() - min_bytes());
    ar& actions_;
}

template<class Archive>
inline void ofp_table_stats::serialize(Archive& ar, const unsigned int)
{
    ar& table_id_;
    ar& bs::make_binary_object(pad_, sizeof pad_);
    ar& bs::make_binary_object(name_, sizeof name_);
    ar& wildcards_;
    ar& max_entries_;
    ar& active_count_;
    ar& lookup_count_;
    ar& matched_count_;
}

template<class Archive>
inline void ofp_queue_prop_list::serialize(Archive& ar, const unsigned int)
{
    // For loading we rely on length to infer the size
    std::size_t i = 0;
    std::size_t byte_count = length_;
    if (Archive::is_loading::value) {
        while (byte_count > 0) {
            ofp_queue_prop qp;
            ar& qp;
            list_[i] = reinterpret_cast<ofp_queue_prop*>(storage_[i]);
            qp.factory(ar, const_cast<ofp_queue_prop*>(list_[i]));
            byte_count -= list_[i++]->len();
        }
    } else {
        while (byte_count > 0) {
            byte_count -= list_[i]->len();
            const_cast<ofp_queue_prop*>(list_[i++])->factory(ar, NULL);
        }
    }
}

template<class Archive>
inline void ofp_queue_prop_min_rate::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_queue_prop>(*this);
    ar& rate_;
    ar& bs::make_binary_object(pad_, sizeof pad_);
}

template<class Archive>
inline void ofp_port_stats::serialize(Archive& ar, const unsigned int)
{
    ar& port_no_;
    ar& bs::make_binary_object(pad_, sizeof pad_);
    ar& rx_packets_;
    ar& tx_packets_;
    ar& rx_bytes_;
    ar& tx_bytes_;
    ar& rx_dropped_;
    ar& tx_dropped_;
    ar& rx_errors_;
    ar& tx_errors_;
    ar& rx_frame_err_;
    ar& rx_over_err_;
    ar& rx_crc_err_;
    ar& collisions_;
}

template<class Archive>
inline void ofp_queue_prop::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value || property() == OFPQT_INVALID)
    {
        ar& property_;
        ar& len_;
        ar& bs::make_binary_object(pad_, sizeof pad_);
    }
}

template<class Archive>
inline void ofp_port_mod::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_msg>(*this);
    ar& port_no_;
    ar& hw_addr_;
    ar& config_;
    ar& mask_;
    ar& advertise_;
    ar& bs::make_binary_object(pad_, sizeof pad_);
}

template<class Archive>
inline void ofp_flow_removed::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_msg>(*this);
    ar& match_;
    ar& cookie_;
    ar& priority_;
    ar& reason_;
    ar& bs::make_binary_object(pad_, sizeof pad_);
    ar& duration_sec_;
    ar& duration_nsec_;
    ar& idle_timeout_;
    ar& bs::make_binary_object(pad2_, sizeof pad2_);
    ar& packet_count_;
    ar& byte_count_;
}

template<class Archive>
inline void ofp_action_tp_port::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_action>(*this);
    ar& tp_port_;
    ar& bs::make_binary_object(pad_, sizeof pad_);
}

template<class Archive>
inline void ofp_action_vendor::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_action>(*this);
    if (Archive::is_saving::value || vendor() == OFPVT_INVALID)
        ar& vendor_;
}

template<class Archive>
inline void ofp_action_vlan_vid::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_action>(*this);
    ar& vlan_vid_;
    ar& bs::make_binary_object(pad_, sizeof pad_);
}

template<class Archive>
inline void ofp_action_output::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_action>(*this);
    ar& port_;
    ar& max_len_;
}

template<class Archive>
inline void ofp_action_enqueue::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value)
        ar& bs::base_object<ofp_action>(*this);
    ar& port_;
    ar& bs::make_binary_object(pad_, sizeof pad_);
    ar& queue_id_;
}

template<class Archive>
inline void ofp_action_dl_dst::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value) ar& bs::base_object<ofp_action_dl_addr>(*this);
}

template<class Archive>
inline void ofp_action_dl_src::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value) ar& bs::base_object<ofp_action_dl_addr>(*this);
}

template<class Archive>
inline void ofp_action_nw_dst::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value) ar& bs::base_object<ofp_action_nw_addr>(*this);
}

template<class Archive>
inline void ofp_action_nw_src::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value) ar& bs::base_object<ofp_action_nw_addr>(*this);
}

template<class Archive>
inline void ofp_action_strip_vlan::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value) ar& bs::base_object<ofp_action>(*this);
}

template<class Archive>
inline void ofp_action_tp_dst::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value) ar& bs::base_object<ofp_action_tp_port>(*this);
}

template<class Archive>
inline void ofp_action_tp_src::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value) ar& bs::base_object<ofp_action_tp_port>(*this);
}

template<class Archive>
inline void ofp_barrier_reply::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value) ar& bs::base_object<ofp_msg>(*this);
}

template<class Archive>
inline void ofp_barrier_request::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value) ar& bs::base_object<ofp_msg>(*this);
}

template<class Archive>
inline void ofp_desc_stats_request::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value) ar& bs::base_object<ofp_msg>(*this);
}

template<class Archive>
inline void ofp_echo_reply::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value) ar& bs::base_object<ofp_msg>(*this);
    const uint8_t* payload = boost::asio::buffer_cast<const uint8_t*>(payload_buf_);
    uint8_t size = length() - min_bytes();
    ar& bs::make_binary_object(const_cast<uint8_t*>(payload), size);
}

template<class Archive>
inline void ofp_echo_request::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value) ar& bs::base_object<ofp_msg>(*this);
    const uint8_t* payload = boost::asio::buffer_cast<const uint8_t*>(payload_buf_);
    uint8_t size = length() - min_bytes();
    ar& bs::make_binary_object(const_cast<uint8_t*>(payload), size);
    payload_buf_ = boost::asio::buffer(payload, size);
}

template<class Archive>
inline void ofp_features_request::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value) ar& bs::base_object<ofp_msg>(*this);
}

template<class Archive>
inline void ofp_get_config_reply::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value) ar& bs::base_object<ofp_switch_config>(*this);
}

template<class Archive>
inline void ofp_get_config_request::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value) ar& bs::base_object<ofp_msg>(*this);
}

template<class Archive>
inline void ofp_hello::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value) ar& bs::base_object<ofp_msg>(*this);
}

template<class Archive>
inline void ofp_queue_prop_none::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value) ar& bs::base_object<ofp_queue_prop>(*this);
}

template<class Archive>
inline void ofp_set_config::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value) ar& bs::base_object<ofp_switch_config>(*this);
}

template<class Archive>
inline void ofp_table_stats_request::serialize(Archive& ar, const unsigned int)
{
    if (Archive::is_saving::value) ar& bs::base_object<ofp_stats_request>(*this);
}

// Misc
template<typename T>
inline const T* ofp_match::cast_check(boost::asio::const_buffer packet) const
{
    if (boost::asio::buffer_size(packet) < sizeof(T))
    {
        return NULL;
    }
    else
    {
        return boost::asio::buffer_cast<const T*>(packet);
    }
}

// TODO: implement serialization
// TODO: error handling
inline void ofp_match::from_packet(const uint32_t in_port_, boost::asio::const_buffer packet)
{
    in_port(in_port_);
    dl_vlan(OFP_VLAN_NONE);
    const eth_header* eth = cast_check<const eth_header>(packet);
    packet = packet + sizeof(eth_header);

    if (!eth)
    {
        // "Packet length %zu less than minimum Ethernet packet %d"
        return;
    }

    if (ntohs(eth->eth_type) >= ethernet::ETH2_CUTOFF)
    {
        /* This is an Ethernet II frame */
        dl_type(ntohs(eth->eth_type));
    }
    else
    {
        /* This is an 802.2 frame */
        const llc_snap_header* h = cast_check<const llc_snap_header>(packet);
        if (!h)
            return;
        if (h->llc.llc_dsap == LLC_DSAP_SNAP
            && h->llc.llc_ssap == LLC_SSAP_SNAP
            && h->llc.llc_cntl == LLC_CNTL_SNAP
            && !memcmp(h->snap.snap_org, SNAP_ORG_ETHERNET,
                       sizeof h->snap.snap_org))
        {
            dl_type(ntohs(h->snap.snap_type));
            packet = packet + sizeof *h;
        }
        else
        {
            dl_type(OFP_DL_TYPE_NOT_ETH_TYPE);
            packet = packet + sizeof(llc_header);
        }
    }

    /* Check for a VLAN tag */
    if (dl_type() == ETH_TYPE_VLAN)
    {
        const vlan_header* vh = cast_check<const vlan_header>(packet);
        if (!vh)
            return;
        packet = packet + sizeof(vlan_header);
        dl_type(ntohs(vh->vlan_next_type));
        dl_vlan(ntohs(vh->vlan_tci) & VLAN_VID);
        dl_vlan_pcp((ntohs(vh->vlan_tci) & VLAN_PCP_MASK) >> VLAN_PCP_SHIFT);
    }
    
    dl_src(ethernetaddr(eth->eth_src));
    dl_dst(ethernetaddr(eth->eth_dst));

    if (dl_type() == ETH_TYPE_IP)
    {
        const ip_header* ip = cast_check<const ip_header>(packet);
        if (!ip)
            return;
        packet = packet + sizeof(ip_header);

        nw_src(ntohl(ip->ip_src));
        nw_dst(ntohl(ip->ip_dst));
        nw_proto(ip->ip_proto);

        if (!ip_::is_fragment(ip->ip_frag_off))
        {
            if (nw_proto() == ip_::proto::TCP)
            {
                const tcp_header* tcp = cast_check<const tcp_header>(packet);
                if (!tcp)
                    return;
                packet = packet + sizeof(tcp_header);
                tp_src(ntohs(tcp->tcp_src));
                tp_dst(ntohs(tcp->tcp_dst));
            }
            else if (nw_proto() == ip_::proto::UDP)
            {
                const udp_header* udp = cast_check<const udp_header>(packet);
                if (!udp)
                    return;
                packet = packet + sizeof(udp_header);
                tp_src(ntohs(udp->udp_src));
                tp_dst(ntohs(udp->udp_dst));
            }
            else if (nw_proto() == ip_::proto::ICMP)
            {
                const icmp_header* icmp = cast_check<const icmp_header>(packet);
                if (!icmp)
                    return;
                packet = packet + sizeof(icmp_header);
                tp_src(icmp->icmp_type);
                tp_dst(icmp->icmp_code);
            }
        }
    }
    else if (dl_type() == ETH_TYPE_ARP)
    {
        const arp_eth_header* arp = cast_check<const arp_eth_header>(packet);
        if (!arp)
            return;
        packet = packet + sizeof(arp_eth_header);
        if (ntohs(arp->ar_pro) == ARP_PRO_IP
            && arp->ar_pln == 4/*IP_ADDR_LEN*/)
        {
            nw_src(ntohl(arp->ar_spa));
            nw_dst(ntohl(arp->ar_tpa));
        }
        nw_proto(ntohs(arp->ar_op) & 0xff);
    }
}

inline bool ofp_match::operator==(const ofp_match& that)
{
    return (in_port_ == that.in_port_) &&
           (dl_vlan_ == that.dl_vlan_) &&
           (dl_vlan_pcp_ == that.dl_vlan_pcp_) &&
           (dl_src_ == that.dl_src_) &&
           (dl_dst_ == that.dl_dst_) &&
           (dl_type_ == that.dl_type_) &&
           (nw_src_ == that.nw_src_) &&
           (nw_dst_ == that.nw_dst_) &&
           (nw_proto_ == that.nw_proto_) &&
           (nw_tos_ == that.nw_tos_) &&
           (tp_src_ == that.tp_src_) &&
           (tp_dst_ == that.tp_dst_);
}

inline bool ofp_match::operator!=(const ofp_match& that)
{
    return !(*this == that);
}

// Hash

inline std::size_t hash_value(const ofp_match& match)
{
    std::size_t seed = 0;
    boost::hash_combine(seed, match.wildcards());
    boost::hash_combine(seed, match.in_port());
    boost::hash_combine(seed, match.dl_vlan());
    boost::hash_combine(seed, match.dl_vlan_pcp());
    boost::hash_combine(seed, match.dl_src());
    boost::hash_combine(seed, match.dl_dst());
    boost::hash_combine(seed, match.dl_type());
    boost::hash_combine(seed, match.nw_src());
    boost::hash_combine(seed, match.nw_dst());
    boost::hash_combine(seed, match.nw_proto());
    boost::hash_combine(seed, match.nw_tos());
    boost::hash_combine(seed, match.tp_src());
    boost::hash_combine(seed, match.tp_dst());
    return seed;
}

} // namespace v1
} // namespace openflow
} // namespace vigil

/*
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_action_dl_addr)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_action_dl_dst)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_action_dl_src)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_action_enqueue)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_action_nw_addr)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_action_nw_dst)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_action_nw_src)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_action_nw_tos)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_action_output)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_action_strip_vlan)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_action_tp_dst)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_action_tp_port)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_action_tp_src)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_action_vendor)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_action_vlan_pcp)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_action_vlan_vid)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_aggregate_stats_reply)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_aggregate_stats_request)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_barrier_reply)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_barrier_request)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_desc_stats_reply)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_desc_stats_request)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_echo_reply)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_echo_request)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_error_msg)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_features_reply)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_features_request)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_flow_mod)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_flow_removed)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_flow_stats_reply)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_flow_stats_request)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_get_config_reply)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_get_config_request)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_hello)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_packet_in)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_packet_out)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_port_mod)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_port_stats_reply)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_port_stats_request)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_port_status)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_queue_get_config_reply)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_queue_get_config_request)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_queue_prop_min_rate)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_queue_prop_none)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_queue_stats_reply)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_queue_stats_request)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_set_config)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_stats_reply)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_stats_request)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_switch_config)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_table_stats_reply)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_table_stats_request)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_vendor)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_vendor_stats_reply)
BOOST_CLASS_EXPORT_KEY(vigil::openflow::v1::ofp_vendor_stats_request)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_action_dl_addr, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_action_dl_dst, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_action_dl_src, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_action_enqueue, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_action_nw_addr, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_action_nw_dst, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_action_nw_src, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_action_nw_tos, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_action_output, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_action_strip_vlan, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_action_tp_dst, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_action_tp_port, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_action_tp_src, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_action_vendor, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_action_vlan_pcp, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_action_vlan_vid, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_aggregate_stats_reply, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_aggregate_stats_request, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_barrier_reply, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_barrier_request, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_desc_stats_reply, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_desc_stats_request, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_echo_reply, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_echo_request, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_error_msg, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_features_reply, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_features_request, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_flow_mod, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_flow_removed, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_flow_stats_reply, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_flow_stats_request, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_get_config_reply, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_get_config_request, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_hello, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_packet_in, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_packet_out, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_port_mod, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_port_stats_reply, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_port_stats_request, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_port_status, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_queue_get_config_reply, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_queue_get_config_request, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_queue_prop_min_rate, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_queue_prop_none, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_queue_stats_reply, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_queue_stats_request, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_set_config, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_stats_reply, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_stats_request, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_switch_config, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_table_stats_reply, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_table_stats_request, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_vendor, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_vendor_stats_reply, boost::serialization::object_serializable)
BOOST_CLASS_IMPLEMENTATION(vigil::openflow::v1::ofp_vendor_stats_request, boost::serialization::object_serializable)
*/
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_action_dl_addr, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_action_dl_dst, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_action_dl_src, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_action_enqueue, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_action_nw_addr, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_action_nw_dst, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_action_nw_src, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_action_nw_tos, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_action_output, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_action_strip_vlan, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_action_tp_dst, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_action_tp_port, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_action_tp_src, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_action_vendor, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_action_vlan_pcp, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_action_vlan_vid, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_aggregate_stats_reply, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_aggregate_stats_request, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_barrier_reply, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_barrier_request, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_desc_stats_reply, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_desc_stats_request, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_echo_reply, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_echo_request, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_error_msg, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_features_reply, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_features_request, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_flow_mod, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_flow_removed, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_flow_stats_reply, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_flow_stats_request, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_get_config_reply, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_get_config_request, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_hello, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_packet_in, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_packet_out, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_port_mod, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_port_stats_reply, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_port_stats_request, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_port_status, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_queue_get_config_reply, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_queue_get_config_request, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_queue_prop_min_rate, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_queue_prop_none, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_queue_stats_reply, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_queue_stats_request, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_set_config, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_stats_reply, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_stats_request, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_switch_config, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_table_stats_reply, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_table_stats_request, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_vendor, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_vendor_stats_reply, boost::serialization::track_never)
BOOST_CLASS_TRACKING(vigil::openflow::v1::ofp_vendor_stats_request, boost::serialization::track_never)

#endif /* openflow-inl-1.0.hh */

