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

#ifndef OPENFLOW_OF1_HH
#define OPENFLOW_OF1_HH 1

/* OpenFlow: protocol between controller and datapath. */

#include <array>
#include <stdint.h>

#include <boost/archive/polymorphic_iarchive.hpp>
#include <boost/archive/polymorphic_oarchive.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/function.hpp>
#include <boost/preprocessor.hpp>
#include <boost/unordered_map.hpp>
#include <boost/variant.hpp>

#include <openflow/openflow-defs-1.0.hh>

#include "network_iarchive.hh"
#include "network_oarchive.hh"
#include "netinet++/ethernet.hh"

namespace vigil
{
namespace openflow
{
namespace v1
{

#define OFDEFMEM(TYPE,VAR) \
    public: \
    const TYPE& VAR() const { return VAR##_; } \
    OFCLASS& VAR(const TYPE& v) { VAR##_ = v; return *this; } \
    private: \
    TYPE VAR##_;

#define OFBOILERPLATE() \
    public: \
    template<class Archive> void serialize(Archive&, unsigned int); \
    virtual const char* name() const { return BOOST_PP_STRINGIZE(OFCLASS); }

inline uint32_t next_xid()
{
    static uint32_t xid = 1;
    xid = (xid + 1) % (OFP_MAX_XID);
    return xid;
}

typedef boost::variant <
network_iarchive&,
network_oarchive&,
boost::archive::polymorphic_iarchive&,
boost::archive::polymorphic_oarchive&
> ofp_archive_type;


//
// 1. OpenFlow Header

//    ofp_msg

// Base class for all OpenFlow messages

class ofp_msg
{
#undef OFCLASS
#define OFCLASS ofp_msg
    OFBOILERPLATE();
public:
    typedef boost::function<void(ofp_archive_type, ofp_msg*, ofp_msg&)> factory_t;

    enum ofp_type
    {
        /* Immutable messages. */
        OFPT_HELLO,               /* Symmetric message */
        OFPT_ERROR,               /* Symmetric message */
        OFPT_ECHO_REQUEST,        /* Symmetric message */
        OFPT_ECHO_REPLY,          /* Symmetric message */
        OFPT_VENDOR,              /* Symmetric message */

        /* Switch configuration messages. */
        OFPT_FEATURES_REQUEST,    /* Controller/switch message */
        OFPT_FEATURES_REPLY,      /* Controller/switch message */
        OFPT_GET_CONFIG_REQUEST,  /* Controller/switch message */
        OFPT_GET_CONFIG_REPLY,    /* Controller/switch message */
        OFPT_SET_CONFIG,          /* Controller/switch message */

        /* Asynchronous messages. */
        OFPT_PACKET_IN,           /* Async message */
        OFPT_FLOW_REMOVED,        /* Async message */
        OFPT_PORT_STATUS,         /* Async message */

        /* Controller command messages. */
        OFPT_PACKET_OUT,          /* Controller/switch message */
        OFPT_FLOW_MOD,            /* Controller/switch message */
        OFPT_PORT_MOD,            /* Controller/switch message */

        /* Statistics messages. */
        OFPT_STATS_REQUEST,       /* Controller/switch message */
        OFPT_STATS_REPLY,         /* Controller/switch message */

        /* Barrier messages. */
        OFPT_BARRIER_REQUEST,     /* Controller/switch message */
        OFPT_BARRIER_REPLY,       /* Controller/switch message */

        /* Queue configuration messages. */
        OFPT_QUEUE_GET_CONFIG_REQUEST,  /* Controller/switch message */
        OFPT_QUEUE_GET_CONFIG_REPLY,    /* Controller/switch message */

        OFPT_INVALID = 0xff
    };

    ofp_msg(uint8_t type_ = OFPT_INVALID, uint16_t length_ = 8,
            uint32_t xid_ = 0)
    {
        version(OFP_VERSION);
        type(type_);
        length(length_);
        if (xid_ == 0)
            xid(next_xid());
        else
            xid(xid_);
    }

    static std::size_t min_bytes() {
        return 8;
    }

    void clear()
    {
        type_ = OFPT_INVALID;
    }

    static void init();
    static uint8_t static_type()
    {
        return OFPT_INVALID;
    }

    void factory(ofp_archive_type, ofp_msg*);
    static void register_factory(uint8_t, factory_t);

    OFDEFMEM(uint8_t, version);    // openflow version
    OFDEFMEM(uint8_t, type);       // message type
    OFDEFMEM(uint16_t, length);    // message length including header
    OFDEFMEM(uint32_t, xid);       // transaction id
private:
    typedef boost::unordered_map<uint8_t, factory_t> factory_map_t;
    static factory_map_t factory_map;
};


//
// 2. Common Structures
//    Port, Queue, Flow Match, Flow Action

// 2.1. Port Structures
//      ofp_phy_port

/* Description of a physical port */
class ofp_phy_port
{
#undef OFCLASS
#define OFCLASS ofp_phy_port
    OFBOILERPLATE();
public:
    /* Port numbering.  Physical ports are numbered starting from 1. */
    enum ofp_port
    {
        /* Maximum number of physical switch ports. */
        OFPP_MAX = 0xff00,

        OFPP_MAX_NOX = 0xff,

        /* Fake output "ports". */
        OFPP_IN_PORT    = 0xfff8,  /* Send the packet out the input port.  This
                                      virtual port must be explicitly used
                                      in order to send back out of the input
                                      port. */
        OFPP_TABLE      = 0xfff9,  /* Perform actions in flow table.
                                      NB: This can only be the destination
                                      port for packet-out messages. */
        OFPP_NORMAL     = 0xfffa,  /* Process with normal L2/L3 switching. */
        OFPP_FLOOD      = 0xfffb,  /* All physical ports except input port and
                                      those disabled by STP. */
        OFPP_ALL        = 0xfffc,  /* All physical ports except input port. */
        OFPP_CONTROLLER = 0xfffd,  /* Send to controller. */
        OFPP_LOCAL      = 0xfffe,  /* Local openflow "port". */
        OFPP_NONE       = 0xffff   /* Not associated with a physical port. */
    };
    /* Features of physical ports available in a datapath. */
    enum ofp_port_features
    {
        OFPPF_10MB_HD    = 1 << 0,  /* 10 Mb half-duplex rate support. */
        OFPPF_10MB_FD    = 1 << 1,  /* 10 Mb full-duplex rate support. */
        OFPPF_100MB_HD   = 1 << 2,  /* 100 Mb half-duplex rate support. */
        OFPPF_100MB_FD   = 1 << 3,  /* 100 Mb full-duplex rate support. */
        OFPPF_1GB_HD     = 1 << 4,  /* 1 Gb half-duplex rate support. */
        OFPPF_1GB_FD     = 1 << 5,  /* 1 Gb full-duplex rate support. */
        OFPPF_10GB_FD    = 1 << 6,  /* 10 Gb full-duplex rate support. */
        OFPPF_COPPER     = 1 << 7,  /* Copper medium. */
        OFPPF_FIBER      = 1 << 8,  /* Fiber medium. */
        OFPPF_AUTONEG    = 1 << 9,  /* Auto-negotiation. */
        OFPPF_PAUSE      = 1 << 10, /* Pause. */
        OFPPF_PAUSE_ASYM = 1 << 11  /* Asymmetric pause. */
    };
    /* Current state of the physical port.  These are not configurable from
     * the controller.
     */
    enum ofp_port_state
    {
        OFPPS_LINK_DOWN   = 1 << 0, /* No physical link present. */
        /* The OFPPS_STP_* bits have no effect on switch operation.  The
         * controller must adjust OFPPC_NO_RECV, OFPPC_NO_FWD, and
         * OFPPC_NO_PACKET_IN appropriately to fully implement an 802.1D spanning
         * tree. */
        OFPPS_STP_LISTEN  = 0 << 8, /* Not learning or relaying frames. */
        OFPPS_STP_LEARN   = 1 << 8, /* Learning but not relaying frames. */
        OFPPS_STP_FORWARD = 2 << 8, /* Learning and relaying frames. */
        OFPPS_STP_BLOCK   = 3 << 8, /* Not part of spanning tree. */
        OFPPS_STP_MASK    = 3 << 8  /* Bit mask for OFPPS_STP_* values. */
    };
    /* Flags to indicate behavior of the physical port.  These flags are
     * used in ofp_phy_port to describe the current configuration.  They are
     * used in the ofp_port_mod message to configure the port's behavior.
     */
    enum ofp_port_config
    {
        OFPPC_PORT_DOWN    = 1 << 0,  /* Port is administratively down. */

        OFPPC_NO_STP       = 1 << 1,  /* Disable 802.1D spanning tree on port. */
        OFPPC_NO_RECV      = 1 << 2,  /* Drop all packets except 802.1D spanning
                                         tree packets. */
        OFPPC_NO_RECV_STP  = 1 << 3,  /* Drop received 802.1D STP packets. */
        OFPPC_NO_FLOOD     = 1 << 4,  /* Do not include this port when flooding. */
        OFPPC_NO_FWD       = 1 << 5,  /* Drop packets forwarded to port. */
        OFPPC_NO_PACKET_IN = 1 << 6   /* Do not send packet-in msgs for port. */
    };

    ofp_phy_port()
        : port_no_(0), hw_addr_(), config_(0), state_(0), curr_(0),
          advertised_(0), supported_(0), peer_(0)
    {
        std::fill(name_, name_ + sizeof(name_), '\0');
    }

    static std::size_t min_bytes() {
        return 48;
    }

    uint32_t speed();

    OFDEFMEM(uint16_t, port_no);
    OFDEFMEM(ethernetaddr, hw_addr);
    char name_[OFP_MAX_PORT_NAME_LEN]; /* Null-terminated */

    OFDEFMEM(uint32_t, config);       /* Bitmap of OFPPC_* flags. */
    OFDEFMEM(uint32_t, state);        /* Bitmap of OFPPS_* flags. */

    /* Bitmaps of OFPPF_* that describe features.  All bits zeroed if
     * unsupported or unavailable. */
    OFDEFMEM(uint32_t, curr);         /* Current features. */
    OFDEFMEM(uint32_t, advertised);   /* Features being advertised by the port. */
    OFDEFMEM(uint32_t, supported);    /* Features supported by the port. */
    OFDEFMEM(uint32_t, peer);         /* Features advertised by peer. */
};

// 2.2. Queue Structures
//      ofp_packet_queue, ofp_queue_prop, ofp_queue_prop_min_rate

/* Common description for a queue. */
class ofp_queue_prop
{
#undef OFCLASS
#define OFCLASS ofp_queue_prop
    OFBOILERPLATE();
public:
    typedef boost::function<void(ofp_archive_type, ofp_queue_prop*, ofp_queue_prop&)> factory_t;

    enum ofp_queue_properties
    {
        OFPQT_NONE = 0,       /* No property defined for queue (default). */
        OFPQT_MIN_RATE,       /* Minimum datarate guaranteed. */
        /* Other types should be added here
         * (i.e. max rate, precedence, etc). */
        OFPQT_INVALID = 0xffff
    };

    ofp_queue_prop(uint16_t property_ = OFPQT_NONE, uint16_t len_ = 0)
    {
        property(property_);
        len(len_);
        std::fill(pad_, pad_ + sizeof(pad_), '\0');
    }

    static void init();

    static std::size_t min_bytes() {
        return 8;
    }

    void factory(ofp_archive_type, ofp_queue_prop*);
    static void register_factory(uint16_t, factory_t);

private:
    typedef boost::unordered_map<uint16_t, factory_t> factory_map_t;
    static factory_map_t factory_map;

    OFDEFMEM(uint16_t, property);   /* One of OFPQT_. */
    OFDEFMEM(uint16_t, len);        /* Length of property, including this header. */
    uint8_t pad_[4];
};

class ofp_queue_prop_none : public ofp_queue_prop
{
#undef OFCLASS
#define OFCLASS ofp_queue_prop_none
    OFBOILERPLATE();
public:
    ofp_queue_prop_none() : ofp_queue_prop(OFPQT_NONE, 0) {}
    ofp_queue_prop_none(ofp_queue_prop& oqp) : ofp_queue_prop(oqp) {}

    static uint16_t static_type()
    {
        return OFPQT_NONE;
    }
};

class ofp_queue_prop_list
{
#undef OFCLASS
#define OFCLASS ofp_queue_prop_list
    OFBOILERPLATE();
public:
    ofp_queue_prop_list() : length_(0), elems_(0) {}
    ofp_queue_prop_list& push_back(const ofp_queue_prop*& queue_prop) {
        list_[elems_++] = queue_prop;
        length_ += queue_prop->len();
        return *this;
    }

    const std::size_t& length() {
        return length_;
    }
    void length(const std::size_t& length) {
        length_ = length;
    }

private:
    std::size_t length_;
    std::size_t elems_;
    std::array<const ofp_queue_prop*, OFP_MAX_QUEUE_PROP_COUNT> list_;
    uint8_t storage_[OFP_MAX_QUEUE_PROP_COUNT][OFP_MAX_QUEUE_PROP_BYTES];
};

/* Min-Rate queue property description. */
class ofp_queue_prop_min_rate : public ofp_queue_prop
{
#undef OFCLASS
#define OFCLASS ofp_queue_prop_min_rate
    OFBOILERPLATE();
public:
    // TODO FIX LEN
    ofp_queue_prop_min_rate() : ofp_queue_prop(OFPQT_MIN_RATE), rate_(0)
    {
        std::fill(pad_, pad_ + sizeof(pad_), '\0');
    }

    ofp_queue_prop_min_rate(ofp_queue_prop& oqp) : ofp_queue_prop(oqp) {}

    static std::size_t min_bytes() {
        return 16;
    }

    static uint16_t static_type()
    {
        return OFPQT_MIN_RATE;
    }


private:
    OFDEFMEM(uint16_t, rate);       /* In 1/10 of a percent; >1000 -> disabled. */
    uint8_t pad_[6];                /* 64-bit alignment */
};

/* Full description for a queue. */
class ofp_packet_queue
{
#undef OFCLASS
#define OFCLASS ofp_packet_queue
    OFBOILERPLATE();
public:
    ofp_packet_queue(uint32_t queue_id_ = 0, uint16_t len_ = 0)
    {
        queue_id(queue_id_);
        len(len_);
    }

    static std::size_t min_bytes() {
        return 8;
    }

private:
    OFDEFMEM(uint32_t, queue_id);    /* id for the specific queue. */
    OFDEFMEM(uint16_t, len);         /* Length in bytes of this queue desc. */
    uint8_t pad_[2];                 /* 64-bit alignment. */
    ofp_queue_prop_list properties_; /* List of properties. */
};

// 2.3. Flow Match Structures
//      ofp_match

/* Fields to match against flows */
class ofp_match
{
#undef OFCLASS
#define OFCLASS ofp_match
    OFBOILERPLATE();
public:
    ofp_match()
        : dl_src_(), dl_dst_(), dl_vlan_pcp_(0),
          dl_type_(0), nw_tos_(0), nw_proto_(0), nw_src_(0), nw_dst_(0),
          tp_src_(0), tp_dst_(0)
    {
        dl_vlan(OFP_VLAN_NONE);
    }

    void from_packet(const uint32_t in_port, boost::asio::const_buffer packet);

    static std::size_t min_bytes() {
        return 40;
    }

    bool operator==(const ofp_match&);
    bool operator!=(const ofp_match&);

private:
    OFDEFMEM(uint32_t, wildcards);       /* Wildcard fields. */
    OFDEFMEM(uint16_t, in_port);         /* Input switch port. */
    OFDEFMEM(ethernetaddr, dl_src);      /* Ethernet source address. */
    OFDEFMEM(ethernetaddr, dl_dst);      /* Ethernet destination address. */
    OFDEFMEM(uint16_t, dl_vlan);         /* Input VLAN id. */
    OFDEFMEM(uint8_t, dl_vlan_pcp);      /* Input VLAN priority. */
    uint8_t pad1_[1];                    /* Align to 64-bits */
    OFDEFMEM(uint16_t, dl_type);         /* Ethernet frame type. */
    OFDEFMEM(uint8_t, nw_tos);           /* IP ToS (actually DSCP field, 6 bits). */
    OFDEFMEM(uint8_t, nw_proto);         /* IP protocol or lower 8 bits of
                                          * ARP opcode. */
    uint8_t pad2_[2];                    /* Align to 64-bits */
    OFDEFMEM(uint32_t, nw_src);          /* IP source address. */
    OFDEFMEM(uint32_t, nw_dst);          /* IP destination address. */
    OFDEFMEM(uint16_t, tp_src);          /* TCP/UDP source port. */
    OFDEFMEM(uint16_t, tp_dst);          /* TCP/UDP destination port. */

    template<typename T> const T cast_check(boost::asio::const_buffer) const;
};

// 2.4. Flow Action Structures
//      ofp_action_X for X in
//      { output, enqueue, strip_vlan, vlan_vid, vlan_pcp, dl_addr, nw_addr,
//       nw_tos, tp_port, vendor }

class ofp_action
{
#undef OFCLASS
#define OFCLASS ofp_action
    OFBOILERPLATE();
public:
    typedef boost::function<void(ofp_archive_type, ofp_action*, ofp_action&)> factory_t;

    enum ofp_action_type
    {
        OFPAT_OUTPUT,           /* Output to switch port. */
        OFPAT_SET_VLAN_VID,     /* Set the 802.1q VLAN id. */
        OFPAT_SET_VLAN_PCP,     /* Set the 802.1q priority. */
        OFPAT_STRIP_VLAN,       /* Strip the 802.1q header. */
        OFPAT_SET_DL_SRC,       /* Ethernet source address. */
        OFPAT_SET_DL_DST,       /* Ethernet destination address. */
        OFPAT_SET_NW_SRC,       /* IP source address. */
        OFPAT_SET_NW_DST,       /* IP destination address. */
        OFPAT_SET_NW_TOS,       /* IP ToS (DSCP field, 6 bits). */
        OFPAT_SET_TP_SRC,       /* TCP/UDP source port. */
        OFPAT_SET_TP_DST,       /* TCP/UDP destination port. */
        OFPAT_ENQUEUE,          /* Output to queue.  */
        OFPAT_INVALID = 0xffff - 1,
        OFPAT_VENDOR = 0xffff
    };

    ofp_action(uint16_t type_ = OFPAT_INVALID,
               uint16_t len_ = OFP_ACTION_HEADER_BYTES)
    {
        type(type_);
        len(len_);
    }

    static std::size_t min_bytes() {
        return 8;
    }

    static void init();

    void factory(ofp_archive_type, ofp_action*);
    static void register_factory(uint16_t, factory_t);

private:
    typedef boost::unordered_map<uint16_t, factory_t> factory_map_t;
    static factory_map_t factory_map;

    OFDEFMEM(uint16_t, type);                 /* One of OFPAT_*. */
    OFDEFMEM(uint16_t, len);                  /* Length of action, including this
                                                 header.  This is the length of action,
                                                 including any padding to make it
                                                 64-bit aligned. */
};

class ofp_action_list
{
#undef OFCLASS
#define OFCLASS ofp_action_list
    OFBOILERPLATE();
public:
    ofp_action_list() : length_(0), elems_(0) { }

    ofp_action_list& push_back(ofp_action* action) {
        list_[elems_++] = action;
        length_ += action->len();
        return *this;
    }

    const std::size_t& length() {
        return length_;
    }
    void length(const std::size_t& length) {
        length_ = length;
    }

private:
    std::size_t length_;
    std::size_t elems_;
    std::array<ofp_action*, OFP_MAX_ACTION_COUNT> list_;
    uint8_t storage_[OFP_MAX_ACTION_COUNT][OFP_MAX_ACTION_BYTES];
};

/* Action classure for OFPAT_OUTPUT, which sends packets out 'port'.
 * When the 'port' is the OFPP_CONTROLLER, 'max_len' indicates the max
 * number of bytes to send.  A 'max_len' of zero means no bytes of the
 * packet should be sent.*/
class ofp_action_output : public ofp_action
{
#undef OFCLASS
#define OFCLASS ofp_action_output
    OFBOILERPLATE();
public:
    ofp_action_output()
        : ofp_action(OFPAT_OUTPUT, OFP_ACTION_OUTPUT_BYTES),
          port_(0)
    {
        max_len(OFP_MAX_LEN);
    }

    ofp_action_output(ofp_action& ah) : ofp_action(ah) {}

    static uint16_t static_type()
    {
        return OFPAT_OUTPUT;
    }


private:
    OFDEFMEM(uint16_t, port);                 /* Output port. */
    OFDEFMEM(uint16_t, max_len);              /* Max length to send to controller. */
};

/* OFPAT_ENQUEUE action class: send packets to given queue on port. */
class ofp_action_enqueue : public ofp_action
{
#undef OFCLASS
#define OFCLASS ofp_action_enqueue
    OFBOILERPLATE();
public:
    ofp_action_enqueue()
        : ofp_action(OFPAT_ENQUEUE, OFP_ACTION_ENQUEUE_BYTES), port_(0), queue_id_(0)
    {
        std::fill(pad_, pad_ + sizeof(pad_), '\0');
    }
    ofp_action_enqueue(ofp_action& ah) : ofp_action(ah) {}

    static std::size_t min_bytes() {
        return 16;
    }

    static uint16_t static_type()
    {
        return OFPAT_ENQUEUE;
    }


private:
    OFDEFMEM(uint16_t, port);           /* Port that queue belongs. Should
                                           refer to a valid physical port
                                           (i.e. < OFPP_MAX) or OFPP_IN_PORT. */
    uint8_t pad_[6];                    /* Pad for 64-bit alignment. */
    OFDEFMEM(uint32_t, queue_id);       /* Where to enqueue the packets. */
};

/* Action classure for OFPAT_SET_VLAN_VID. */
class ofp_action_strip_vlan : public ofp_action
{
#undef OFCLASS
#define OFCLASS ofp_action_strip_vlan
    OFBOILERPLATE();
public:
    ofp_action_strip_vlan()
        : ofp_action(OFPAT_STRIP_VLAN) {}
    ofp_action_strip_vlan(ofp_action& ah) : ofp_action(ah) {}
};

/* Action classure for OFPAT_SET_VLAN_VID. */
class ofp_action_vlan_vid : public ofp_action
{
#undef OFCLASS
#define OFCLASS ofp_action_vlan_vid
    OFBOILERPLATE();
public:
    ofp_action_vlan_vid()
        : ofp_action(OFPAT_SET_VLAN_VID, OFP_ACTION_VLAN_VID_BYTES), vlan_vid_(0)
    {
        std::fill(pad_, pad_ + sizeof(pad_), '\0');
    }
    ofp_action_vlan_vid(ofp_action& ah) : ofp_action(ah) {}

    static uint16_t static_type()
    {
        return OFPAT_SET_VLAN_VID;
    }


private:
    OFDEFMEM(uint16_t, vlan_vid);             /* VLAN id. */
    uint8_t pad_[2];
};

/* Action classure for OFPAT_SET_VLAN_PCP. */
class ofp_action_vlan_pcp : public ofp_action
{
#undef OFCLASS
#define OFCLASS ofp_action_vlan_pcp
    OFBOILERPLATE();
public:
    ofp_action_vlan_pcp()
        : ofp_action(OFPAT_SET_VLAN_PCP, OFP_ACTION_VLAN_PCP_BYTES), vlan_pcp_(0)
    {
        std::fill(pad_, pad_ + sizeof(pad_), '\0');
    }
    ofp_action_vlan_pcp(ofp_action& ah) : ofp_action(ah) {}

    static uint16_t static_type()
    {
        return OFPAT_SET_VLAN_PCP;
    }


private:
    OFDEFMEM(uint8_t, vlan_pcp);              /* VLAN priority. */
    uint8_t pad_[3];
};

/* Action classure for OFPAT_SET_DL_SRC/DST. */
class ofp_action_dl_addr : public ofp_action
{
#undef OFCLASS
#define OFCLASS ofp_action_dl_addr
    OFBOILERPLATE();
public:
    ofp_action_dl_addr(uint16_t type_ = OFPAT_INVALID,
                       uint16_t len_ = OFP_ACTION_DL_ADDR_BYTES)
        : ofp_action(type_, len_), dl_addr_()
    {
        std::fill(pad_, pad_ + sizeof(pad_), '\0');
    }
    ofp_action_dl_addr(ofp_action& ah) : ofp_action(ah) {}

    static std::size_t min_bytes() {
        return 16;
    }

    OFDEFMEM(ethernetaddr, dl_addr);
private:
    uint8_t pad_[6];
};

class ofp_action_dl_src : public ofp_action_dl_addr
{
#undef OFCLASS
#define OFCLASS ofp_action_dl_src
    OFBOILERPLATE();
public:
    ofp_action_dl_src() : ofp_action_dl_addr(OFPAT_SET_DL_SRC) {}
    ofp_action_dl_src(ofp_action& ah) : ofp_action_dl_addr(ah) {}

    static uint16_t static_type()
    {
        return OFPAT_SET_DL_SRC;
    }
};

class ofp_action_dl_dst : public ofp_action_dl_addr
{
#undef OFCLASS
#define OFCLASS ofp_action_dl_dst
    OFBOILERPLATE();
public:
    ofp_action_dl_dst() : ofp_action_dl_addr(OFPAT_SET_DL_DST) {}
    ofp_action_dl_dst(ofp_action& ah) : ofp_action_dl_addr(ah) {}

    static uint16_t static_type()
    {
        return OFPAT_SET_DL_DST;
    }
};

/* Action classure for OFPAT_SET_NW_SRC/DST. */
class ofp_action_nw_addr : public ofp_action
{
#undef OFCLASS
#define OFCLASS ofp_action_nw_addr
    OFBOILERPLATE();
public:
    ofp_action_nw_addr(uint16_t type_ = OFPAT_INVALID,
                       uint16_t len_ = OFP_ACTION_NW_ADDR_BYTES)
        : ofp_action(type_, len_) {}
    ofp_action_nw_addr(ofp_action& ah) : ofp_action(ah) {}

    OFDEFMEM(uint32_t, nw_addr);              /* IP address. */
};

class ofp_action_nw_src : public ofp_action_nw_addr
{
#undef OFCLASS
#define OFCLASS ofp_action_nw_src
    OFBOILERPLATE();
public:
    ofp_action_nw_src() : ofp_action_nw_addr(OFPAT_SET_NW_SRC) {}
    ofp_action_nw_src(ofp_action& ah) : ofp_action_nw_addr(ah) {}

    static uint16_t static_type()
    {
        return OFPAT_SET_NW_SRC;
    }
};

class ofp_action_nw_dst : public ofp_action_nw_addr
{
#undef OFCLASS
#define OFCLASS ofp_action_nw_dst
    OFBOILERPLATE();
public:
    ofp_action_nw_dst() : ofp_action_nw_addr(OFPAT_SET_NW_DST) {}
    ofp_action_nw_dst(ofp_action& ah) : ofp_action_nw_addr(ah) {}

    static uint16_t static_type()
    {
        return OFPAT_SET_NW_DST;
    }
};

/* Action classure for OFPAT_SET_NW_TOS. */
class ofp_action_nw_tos : public ofp_action
{
#undef OFCLASS
#define OFCLASS ofp_action_nw_tos
    OFBOILERPLATE();
public:
    ofp_action_nw_tos(uint8_t nw_tos_ = 0)
        : ofp_action(OFPAT_SET_NW_TOS, OFP_ACTION_NW_TOS_BYTES)
    {
        nw_tos(nw_tos_);
        std::fill(pad_, pad_ + sizeof(pad_), '\0');
    }
    ofp_action_nw_tos(ofp_action& ah) : ofp_action(ah) {}

    static uint16_t static_type()
    {
        return OFPAT_SET_NW_TOS;
    }


private:
    OFDEFMEM(uint8_t, nw_tos);                /* IP ToS (DSCP field, 6 bits). */
    uint8_t pad_[3];
};

/* Action classure for OFPAT_SET_TP_SRC/DST. */
class ofp_action_tp_port : public ofp_action
{
#undef OFCLASS
#define OFCLASS ofp_action_tp_port
    OFBOILERPLATE();
public:
    ofp_action_tp_port(uint16_t type_ = OFPAT_INVALID,
                       uint16_t len_ = OFP_ACTION_TP_PORT_BYTES)
        : ofp_action(type_, len_)
    {
        std::fill(pad_, pad_ + sizeof(pad_), '\0');
    }
    ofp_action_tp_port(ofp_action& ah) : ofp_action(ah) {}

    OFDEFMEM(uint16_t, tp_port);              /* TCP/UDP port. */
    uint8_t pad_[2];
};

class ofp_action_tp_src : public ofp_action_tp_port
{
#undef OFCLASS
#define OFCLASS ofp_action_tp_src
    OFBOILERPLATE();
public:
    ofp_action_tp_src() : ofp_action_tp_port(OFPAT_SET_TP_SRC) {}
    ofp_action_tp_src(ofp_action& ah) : ofp_action_tp_port(ah) {}

    static uint16_t static_type()
    {
        return OFPAT_SET_TP_SRC;
    }
};

class ofp_action_tp_dst : public ofp_action_tp_port
{
#undef OFCLASS
#define OFCLASS ofp_action_tp_dst
    OFBOILERPLATE();
public:
    ofp_action_tp_dst() : ofp_action_tp_port(OFPAT_SET_TP_DST) {}
    ofp_action_tp_dst(ofp_action& ah) : ofp_action_tp_port(ah) {}

    static uint16_t static_type()
    {
        return OFPAT_SET_TP_DST;
    }
};

/* Action header for OFPAT_VENDOR. The rest of the body is vendor-defined. */
class ofp_vendor;
class ofp_action_vendor : public ofp_action
{
#undef OFCLASS
#define OFCLASS ofp_action_vendor
    OFBOILERPLATE();
public:
    typedef boost::function<void(ofp_archive_type, ofp_action_vendor*, ofp_action_vendor&)> factory_t;

    ofp_action_vendor(uint32_t vendor_ = OFPVT_INVALID)
        : ofp_action(OFPAT_VENDOR, OFP_ACTION_VENDOR_HEADER_BYTES)
    {
        vendor(vendor_);
    }
    ofp_action_vendor(ofp_action& ah) : ofp_action(ah) {}

    static void init();
    static uint16_t static_type()
    {
        return OFPAT_VENDOR;
    }

    void factory(ofp_archive_type, ofp_action_vendor*);
    static void register_factory(uint32_t, factory_t);

    OFDEFMEM(uint32_t, vendor);
private:
    typedef boost::unordered_map<uint32_t, factory_t> factory_map_t;
    static factory_map_t factory_map;
};


//
// 3. Controller to Switch Messages
//    Handshake, Switch Configuration, Modify State, Queue Configuration,
//    Read State, Send Packet, Barrier

// 3.1. Handshake Messages
//    ofp_features_reply == ofp_switch_features

/* Switch features. */
class ofp_features_reply : public ofp_msg
{
#undef OFCLASS
#define OFCLASS ofp_features_reply
    OFBOILERPLATE();
public:
    /* Capabilities supported by the datapath. */
    enum ofp_capabilities
    {
        OFPC_FLOW_STATS     = 1 << 0,  /* Flow statistics. */
        OFPC_TABLE_STATS    = 1 << 1,  /* Table statistics. */
        OFPC_PORT_STATS     = 1 << 2,  /* Port statistics. */
        OFPC_STP            = 1 << 3,  /* 802.1d spanning tree. */
        OFPC_RESERVED       = 1 << 4,  /* Reserved, must be zero. */
        OFPC_IP_REASM       = 1 << 5,  /* Can reassemble IP fragments. */
        OFPC_QUEUE_STATS    = 1 << 6,  /* Queue statistics. */
        OFPC_ARP_MATCH_IP   = 1 << 7   /* Match IP addresses in ARP pkts. */
    };

    ofp_features_reply()
        : ofp_msg(OFPT_FEATURES_REPLY, OFP_FEATURES_REPLY_BYTES), datapath_id_(0),
          n_buffers_(0), n_tables_(0), capabilities_(0), actions_(0)
    {
        std::fill(pad_, pad_ + sizeof(pad_), '\0');
    }
    ofp_features_reply(ofp_msg& msg) : ofp_msg(msg) {}

    static std::size_t min_bytes() {
        return 32;
    }

    static uint8_t static_type()
    {
        return OFPT_FEATURES_REPLY;
    }


    /* Datapath unique ID.  The lower 48-bits are for
       a MAC address, while the upper 16-bits are
       implementer-defined. */
    OFDEFMEM(uint64_t, datapath_id);
    OFDEFMEM(uint32_t, n_buffers);    /* Max packets buffered at once. */
    OFDEFMEM(uint8_t, n_tables);      /* Number of tables supported by datapath. */
private:
    uint8_t pad_[3];
    /* Features. */
    OFDEFMEM(uint32_t, capabilities); /* Bitmap of support "ofp_capabilities". */
    OFDEFMEM(uint32_t, actions);      /* Bitmap of supported "ofp_action_type"s. */

    /* Port info.*/
    ofp_phy_port ports_[ofp_phy_port::OFPP_MAX_NOX]; /* Port definitions.  The number of ports
                                                      * is inferred from the length field in
                                                      * the header. */
    /* Defined by Amin */
    std::size_t n_ports_;
};

//typedef ofp_features_reply ofp_switch_features;

// 3.2. Switch Configuration Messages
//    ofp_switch_config

/* Switch configuration. */
class ofp_switch_config : public ofp_msg
{
#undef OFCLASS
#define OFCLASS ofp_switch_config
    OFBOILERPLATE();
public:
    enum ofp_config_flags
    {
        /* Handling of IP fragments. */
        OFPC_FRAG_NORMAL   = 0,  /* No special handling for fragments. */
        OFPC_FRAG_DROP     = 1,  /* Drop fragments. */
        OFPC_FRAG_REASM    = 2,  /* Reassemble (only if OFPC_IP_REASM set). */
        OFPC_FRAG_MASK     = 3
    };

    ofp_switch_config(uint8_t type_ = OFPT_INVALID)
        : ofp_msg(type_, OFP_SWITCH_CONFIG_BYTES),
          flags_(0) {
        miss_send_len(OFP_DEFAULT_MISS_SEND_LEN);
    }
    ofp_switch_config(ofp_msg& msg) : ofp_msg(msg) {}

    static std::size_t min_bytes() {
        return 12;
    }

private:
    OFDEFMEM(uint16_t, flags);            /* OFPC_* flags. */
    OFDEFMEM(uint16_t, miss_send_len);    /* Max bytes of new flow that datapath should
                                             send to the controller. */
};

// 3.3. Modify State Messages
//    ofp_flow_mod, ofp_port_mod

/* Flow setup and teardown (controller -> datapath). */
class ofp_flow_mod : public ofp_msg
{
#undef OFCLASS
#define OFCLASS ofp_flow_mod
    OFBOILERPLATE();
public:
    enum ofp_flow_mod_command
    {
        OFPFC_ADD,              /* New flow. */
        OFPFC_MODIFY,           /* Modify all matching flows. */
        OFPFC_MODIFY_STRICT,    /* Modify entry strictly matching wildcards */
        OFPFC_DELETE,           /* Delete all matching flows. */
        OFPFC_DELETE_STRICT     /* Strictly match wildcards and priority. */
    };

    enum ofp_flow_mod_flags
    {
        OFPFF_SEND_FLOW_REM = 1 << 0,  /* Send flow removed message when flow
                                        * expires or is deleted. */
        OFPFF_CHECK_OVERLAP = 1 << 1,  /* Check for overlapping entries first. */
        OFPFF_EMERG         = 1 << 2   /* Remark this is for emergency. */
    };

    ofp_flow_mod()
        : ofp_msg(OFPT_FLOW_MOD, OFP_FLOW_MOD_BYTES), cookie_(0), command_(0),
          idle_timeout_(0), hard_timeout_(0), flags_(0)
    {
        priority(OFP_DEFAULT_PRIORITY);
        buffer_id(-1);
        out_port(ofp_phy_port::OFPP_NONE);
        if (SEND_FLOW_REMOVED)
            flags_ = OFPFF_SEND_FLOW_REM;
    }
    ofp_flow_mod(ofp_msg& msg) : ofp_msg(msg) {}

    static std::size_t min_bytes() {
        return 72;
    }

    static uint8_t static_type()
    {
        return OFPT_FLOW_MOD;
    }

    ofp_flow_mod& add_action(ofp_action* action)
    {
        length(length() + action->len());
        actions_.push_back(action);
        return *this;
    }

    OFDEFMEM(ofp_match, match);             /* Fields to match */
    OFDEFMEM(uint64_t, cookie);             /* Opaque controller-issued identifier. */

    /* Flow actions. */
    OFDEFMEM(uint16_t, command);            /* One of OFPFC_*. */
    OFDEFMEM(uint16_t, idle_timeout);       /* Idle time before discarding (seconds). */
    OFDEFMEM(uint16_t, hard_timeout);       /* Max time before discarding (seconds). */
    OFDEFMEM(uint16_t, priority);           /* Priority level of flow entry. */
    OFDEFMEM(uint32_t, buffer_id);          /* Buffered packet to apply to (or -1).
                                               Not meaningful for OFPFC_DELETE*. */
    OFDEFMEM(uint16_t, out_port);           /* For OFPFC_DELETE* commands, require
                                               matching entries to include this as an
                                               output port.  A value of OFPP_NONE
                                               indicates no restriction. */
    OFDEFMEM(uint16_t, flags);              /* One of OFPFF_*. */
    ofp_action_list actions_;
};

/* Modify behavior of the physical port */
class ofp_port_mod : public ofp_msg
{
#undef OFCLASS
#define OFCLASS ofp_port_mod
    OFBOILERPLATE();
public:
    ofp_port_mod()
        : ofp_msg(OFPT_PORT_MOD, OFP_PORT_MOD_BYTES), port_no_(0), hw_addr_(), config_(0), mask_(0), advertise_(0)
    {
        std::fill(pad_, pad_ + sizeof(pad_), '\0');
    }
    ofp_port_mod(ofp_msg& msg) : ofp_msg(msg) {}

    static std::size_t min_bytes() {
        return 32;
    }

    static uint8_t static_type()
    {
        return OFPT_PORT_MOD;
    }


    OFDEFMEM(uint16_t, port_no);
    OFDEFMEM(ethernetaddr, hw_addr);  /* The hardware address is not
                                         configurable.  This is used to
                                         sanity-check the request, so it must
                                         be the same as returned in an
                                         ofp_phy_port class. */

    OFDEFMEM(uint32_t, config);       /* Bitmap of OFPPC_* flags. */
    OFDEFMEM(uint32_t, mask);         /* Bitmap of OFPPC_* flags to be changed. */

    OFDEFMEM(uint32_t, advertise);    /* Bitmap of "ofp_port_features"s.  Zero all
                                         bits to prevent any action taking place. */
    uint8_t pad_[4];                  /* Pad to 64-bits. */
};

// 3.4. Queue Configuration Messages
//    ofp_queue_get_config_request, ofp_queue_get_config_reply

/* Query for port queue configuration. */
class ofp_queue_get_config_request : public ofp_msg
{
#undef OFCLASS
#define OFCLASS ofp_queue_get_config_request
    OFBOILERPLATE();
public:
    ofp_queue_get_config_request()
        : ofp_msg(OFPT_QUEUE_GET_CONFIG_REQUEST, OFP_QUEUE_GET_CONFIG_REQUEST_BYTES), port_(0)
    {
        std::fill(pad_, pad_ + sizeof(pad_), '\0');
    }
    ofp_queue_get_config_request(ofp_msg& msg) : ofp_msg(msg) {}

    static std::size_t min_bytes() {
        return 12;
    }

    static uint8_t static_type()
    {
        return OFPT_QUEUE_GET_CONFIG_REQUEST;
    }


private:
    OFDEFMEM(uint16_t, port);        /* Port to be queried. Should refer
                                        to a valid physical port (i.e. < OFPP_MAX) */
    uint8_t pad_[2];                 /* 32-bit alignment. */
};

/* Queue configuration for a given port. */
class ofp_queue_get_config_reply : public ofp_msg
{
#undef OFCLASS
#define OFCLASS ofp_queue_get_config_reply
    OFBOILERPLATE();
public:
    ofp_queue_get_config_reply()
        : ofp_msg(OFPT_QUEUE_GET_CONFIG_REPLY, OFP_QUEUE_GET_CONFIG_REPLY_BYTES), port_(0)
    {
        std::fill(pad_, pad_ + sizeof(pad_), '\0');
    }
    ofp_queue_get_config_reply(ofp_msg& msg) : ofp_msg(msg) {}

    static std::size_t min_bytes() {
        return 16;
    }

    static uint8_t static_type()
    {
        return OFPT_QUEUE_GET_CONFIG_REPLY;
    }


private:
    OFDEFMEM(uint16_t, port);
    uint8_t pad_[6];
    ofp_packet_queue queues_[OFP_MAX_QUEUES]; /* List of configured queues. */
    /* Defined by Amin */
    std::size_t n_queues_;
};

// 3.5. Read State Messages
//      for X in {desc, flow, aggregate, port, queue, vendor}
//         ofp_X_stats_{request, reply}
//         ofp_X_stats if ofp_X_stats_reply may contain a list of ofp_X_stats

class ofp_flow_stats
{
#undef OFCLASS
#define OFCLASS ofp_flow_stats
    OFBOILERPLATE();
public:
    ofp_flow_stats()
        : length_(0), table_id_(0), pad_(0), duration_sec_(0),
          duration_nsec_(0), idle_timeout_(0),
          hard_timeout_(0), cookie_(0), packet_count_(0), byte_count_(0)
    {
        priority(OFP_DEFAULT_PRIORITY);
        std::fill(pad2_, pad2_ + sizeof(pad2_), '\0');
    }

    static std::size_t min_bytes() {
        return 88;
    }

    ofp_flow_stats& add_action(ofp_action* action)
    {
        length_ += action->len();
        actions_.push_back(action);
        return *this;
    }

private:
    OFDEFMEM(uint16_t, length);         /* Length of this entry. */
    OFDEFMEM(uint8_t, table_id);        /* ID of table flow came from. */
    OFDEFMEM(uint8_t, pad);
    ofp_match match_;                   /* Description of fields. */
    OFDEFMEM(uint32_t, duration_sec);   /* Time flow has been alive in seconds. */
    OFDEFMEM(uint32_t, duration_nsec);  /* Time flow has been alive in nanoseconds beyond
                                           duration_sec. */
    OFDEFMEM(uint16_t, priority);       /* Priority of the entry. Only meaningful
                                           when this is not an exact-match entry. */
    OFDEFMEM(uint16_t, idle_timeout);   /* Number of seconds idle before expiration. */
    OFDEFMEM(uint16_t, hard_timeout);   /* Number of seconds before expiration. */
    uint8_t pad2_[6];                   /* Align to 64-bits. */
    OFDEFMEM(uint64_t, cookie);         /* Opaque controller-issued identifier. */
    OFDEFMEM(uint64_t, packet_count);   /* Number of packets in flow. */
    OFDEFMEM(uint64_t, byte_count);     /* Number of bytes in flow. */
    ofp_action_list actions_;           /* Actions. */
};

class ofp_table_stats
{
#undef OFCLASS
#define OFCLASS ofp_table_stats
    OFBOILERPLATE();
public:
    ofp_table_stats()
        : table_id_(0), wildcards_(0), max_entries_(0), active_count_(0),
          lookup_count_(0), matched_count_(0)
    {
        std::fill(pad_, pad_ + sizeof(pad_), '\0');
        std::fill(name_, name_ + sizeof(name_), '\0');
    }

    static std::size_t min_bytes() {
        return 64;
    }

private:
    OFDEFMEM(uint8_t, table_id);       /* Identifier of table.  Lower numbered tables
                                          are consulted first. */
    uint8_t pad_[3];                   /* Align to 32-bits. */
    char name_[OFP_MAX_TABLE_NAME_LEN];
    OFDEFMEM(uint32_t, wildcards);     /* Bitmap of OFPFW_* wildcards that are
                                          supported by the table. */
    OFDEFMEM(uint32_t, max_entries);   /* Max number of entries supported. */
    OFDEFMEM(uint32_t, active_count);  /* Number of active entries. */
    OFDEFMEM(uint64_t, lookup_count);  /* Number of packets looked up in table. */
    OFDEFMEM(uint64_t, matched_count); /* Number of packets that hit table. */
};

/* If a counter is unsupported, set the field to all ones. */
class ofp_port_stats
{
#undef OFCLASS
#define OFCLASS ofp_port_stats
    OFBOILERPLATE();
public:
    ofp_port_stats()
        : port_no_(0), rx_packets_(0), tx_packets_(0), rx_bytes_(0),
          tx_bytes_(0), rx_dropped_(0), tx_dropped_(0), rx_errors_(0),
          tx_errors_(0), rx_frame_err_(0), rx_over_err_(0), rx_crc_err_(0),
          collisions_(0)
    {
        std::fill(pad_, pad_ + sizeof(pad_), '\0');
    }

    static std::size_t min_bytes() {
        return 8;
    }

private:
    OFDEFMEM(uint16_t, port_no);
    uint8_t pad_[6];                   /* Align to 64-bits. */
    OFDEFMEM(uint64_t, rx_packets);    /* Number of received packets. */
    OFDEFMEM(uint64_t, tx_packets);    /* Number of transmitted packets. */
    OFDEFMEM(uint64_t, rx_bytes);      /* Number of received bytes. */
    OFDEFMEM(uint64_t, tx_bytes);      /* Number of transmitted bytes. */
    OFDEFMEM(uint64_t, rx_dropped);    /* Number of packets dropped by RX. */
    OFDEFMEM(uint64_t, tx_dropped);    /* Number of packets dropped by TX. */
    OFDEFMEM(uint64_t, rx_errors);     /* Number of receive errors.  This is a super-set
                                          of more specific receive errors and should be
                                          greater than or equal to the sum of all
                                          rx_*_err values. */
    OFDEFMEM(uint64_t, tx_errors);     /* Number of transmit errors.  This is a super-set
                                          of more specific transmit errors and should be
                                          greater than or equal to the sum of all
                                          tx_*_err values (none currently defined.) */
    OFDEFMEM(uint64_t, rx_frame_err);  /* Number of frame alignment errors. */
    OFDEFMEM(uint64_t, rx_over_err);   /* Number of packets with RX overrun. */
    OFDEFMEM(uint64_t, rx_crc_err);    /* Number of CRC errors. */
    OFDEFMEM(uint64_t, collisions);    /* Number of collisions. */
};

class ofp_queue_stats
{
#undef OFCLASS
#define OFCLASS ofp_queue_stats
    OFBOILERPLATE();
public:
    ofp_queue_stats()
        : port_no_(0), queue_id_(0), tx_bytes_(0), tx_packets_(0), tx_errors_(0)
    {
        std::fill(pad_, pad_ + sizeof(pad_), '\0');
    }

    static std::size_t min_bytes() {
        return 32;
    }

private:
    OFDEFMEM(uint16_t, port_no);
    uint8_t pad_[2];                   /* Align to 32-bits. */
    OFDEFMEM(uint32_t, queue_id);      /* Queue i.d */
    OFDEFMEM(uint64_t, tx_bytes);      /* Number of transmitted bytes. */
    OFDEFMEM(uint64_t, tx_packets);    /* Number of transmitted packets. */
    OFDEFMEM(uint64_t, tx_errors);     /* Number of packets dropped due to overrun. */
};

template<class Type>
class ofp_stats_list
{
#undef OFCLASS
#define OFCLASS ofp_stats_list
    OFBOILERPLATE();
public:
    ofp_stats_list() : length_(0), elems_(0) {}

    const std::size_t& length() {
        return length_;
    }
    void length(const std::size_t& length) {
        length_ = length;
    }

private:
    std::size_t length_;
    std::size_t elems_;
    std::array<Type, OFP_MAX_STATS_PER_REPLY> list_;
};

class ofp_stats : public ofp_msg
{
#undef OFCLASS
#define OFCLASS ofp_stats
    OFBOILERPLATE();
public:
    enum ofp_stats_types
    {
        /* Description of this OpenFlow switch.
         * The request body is empty.
         * The reply body is class ofp_desc_stats. */
        OFPST_DESC,

        /* Individual flow statistics.
         * The request body is class ofp_flow_stats_request.
         * The reply body is an array of class ofp_flow_stats. */
        OFPST_FLOW,

        /* Aggregate flow statistics.
         * The request body is class ofp_aggregate_stats_request.
         * The reply body is class ofp_aggregate_stats_reply. */
        OFPST_AGGREGATE,

        /* Flow table statistics.
         * The request body is empty.
         * The reply body is an array of class ofp_table_stats. */
        OFPST_TABLE,

        /* Physical port statistics.
         * The request body is class ofp_port_stats_request.
         * The reply body is an array of class ofp_port_stats. */
        OFPST_PORT,

        /* Queue statistics for a port
         * The request body defines the port
         * The reply body is an array of class ofp_queue_stats */
        OFPST_QUEUE,

        OFPST_INVALID = 0xffff - 1,
        /* Vendor extension.
         * The request and reply bodies begin with a 32-bit vendor ID, which takes
         * the same form as in "class ofp_vendor_header".  The request and reply
         * bodies are otherwise vendor-defined. */
        OFPST_VENDOR = 0xffff
    };

    ofp_stats(uint8_t type_ = OFPT_INVALID, uint16_t length_ = OFP_HEADER_BYTES,
              uint16_t stat_type_ = OFPST_INVALID, uint16_t stat_flags_ = 0)
        : ofp_msg(type_, length_)
    {
        type(stat_type_);
        flags(stat_flags_);
    }
    ofp_stats(ofp_msg& msg) : ofp_msg(msg) {
    }

private:
    OFDEFMEM(uint16_t, type);              /* One of the OFPST_* constants. */
    OFDEFMEM(uint16_t, flags);             /* OFPSF_REQ_* flags (none yet defined). */
};

// TODO: FIX TYPE CLASH WITH OFP_MSG TYPE
class ofp_stats_request : public ofp_stats
{
#undef OFCLASS
#define OFCLASS ofp_stats_request
    OFBOILERPLATE();
public:
    typedef boost::function<void(ofp_archive_type, ofp_stats_request*, ofp_stats_request&)> factory_t;

    ofp_stats_request(uint16_t type_ = OFPST_INVALID, uint16_t flags_ = 0)
        : ofp_stats(OFPT_STATS_REQUEST, OFP_STATS_REQUEST_BYTES, type_, flags_) {}
    ofp_stats_request(ofp_msg& msg) : ofp_stats(msg) {}

    static std::size_t min_bytes() {
        return 12;
    }

    static void init();
    static uint8_t static_type()
    {
        return OFPT_STATS_REQUEST;
    }

    void factory(ofp_archive_type, ofp_stats_request*);
    static void register_factory(uint16_t, factory_t);

private:
    typedef boost::unordered_map<uint16_t, factory_t> factory_map_t;
    static factory_map_t factory_map;
};

class ofp_stats_reply : public ofp_stats
{
#undef OFCLASS
#define OFCLASS ofp_stats_reply
    OFBOILERPLATE();
public:
    typedef boost::function<void(ofp_archive_type, ofp_stats_reply*, ofp_stats_reply&)> factory_t;

    enum ofp_stats_reply_flags
    {
        OFPSF_REPLY_MORE  = 1 << 0  /* More replies to follow. */
    };

    ofp_stats_reply(uint16_t type_ = OFPST_INVALID, uint16_t flags_ = 0)
        : ofp_stats(OFPT_STATS_REPLY, OFP_STATS_REPLY_BYTES, type_, flags_) {}
    ofp_stats_reply(ofp_msg& msg) : ofp_stats(msg) {}

    static std::size_t min_bytes() {
        return 12;
    }

    static void init();
    static uint8_t static_type()
    {
        return OFPT_STATS_REPLY;
    }

    void factory(ofp_archive_type, ofp_stats_reply*);
    static void register_factory(uint16_t, factory_t);

private:
    typedef boost::unordered_map<uint16_t, factory_t> factory_map_t;
    static factory_map_t factory_map;
};

class ofp_desc_stats_request : public ofp_stats_request
{
#undef OFCLASS
#define OFCLASS ofp_desc_stats_request
    OFBOILERPLATE();
public:
    ofp_desc_stats_request() : ofp_stats_request(OFPST_DESC) {}
    ofp_desc_stats_request(ofp_stats_request& osr) : ofp_stats_request(osr) {}

    static uint16_t static_type()
    {
        return OFPST_DESC;
    }
};

class ofp_flow_stats_request : public ofp_stats_request
{
#undef OFCLASS
#define OFCLASS ofp_flow_stats_request
    OFBOILERPLATE();
public:
    ofp_flow_stats_request()
        : ofp_stats_request(OFPST_FLOW), table_id_(0), pad_(0), out_port_(0) {}
    ofp_flow_stats_request(ofp_stats_request& osr) : ofp_stats_request(osr) {}

    static std::size_t min_bytes() {
        return 44;
    }

    static uint16_t static_type()
    {
        return OFPST_FLOW;
    }


private:
    ofp_match match_;               /* Fields to match. */
    OFDEFMEM(uint8_t, table_id);    /* ID of table to read (from ofp_table_stats),
                                       0xff for all tables or 0xfe for emergency. */
    OFDEFMEM(uint8_t, pad);         /* Align to 32 bits. */
    OFDEFMEM(uint16_t, out_port);   /* Require matching entries to include this
                                       as an output port.  A value of OFPP_NONE
                                       indicates no restriction. */
};

class ofp_aggregate_stats_request : public ofp_stats_request
{
#undef OFCLASS
#define OFCLASS ofp_aggregate_stats_request
    OFBOILERPLATE();
public:
    ofp_aggregate_stats_request()
        : ofp_stats_request(OFPST_AGGREGATE), table_id_(0), pad_(0), out_port_(0) {}
    ofp_aggregate_stats_request(ofp_stats_request& osr) : ofp_stats_request(osr) {}

    static std::size_t min_bytes() {
        return 44;
    }

    static uint16_t static_type()
    {
        return OFPST_AGGREGATE;
    }


private:
    ofp_match match_;               /* Fields to match. */
    OFDEFMEM(uint8_t, table_id);    /* ID of table to read (from ofp_table_stats)
                                       0xff for all tables or 0xfe for emergency. */
    OFDEFMEM(uint8_t, pad);         /* Align to 32 bits. */
    OFDEFMEM(uint16_t, out_port);   /* Require matching entries to include this
                                       as an output port.  A value of OFPP_NONE
                                       indicates no restriction. */
};

class ofp_table_stats_request : public ofp_stats_request
{
#undef OFCLASS
#define OFCLASS ofp_table_stats_request
    OFBOILERPLATE();
public:
    ofp_table_stats_request() : ofp_stats_request(OFPST_TABLE) {}
    ofp_table_stats_request(ofp_stats_request& osr) : ofp_stats_request(osr) {}

    static uint16_t static_type()
    {
        return OFPST_TABLE;
    }
};

class ofp_port_stats_request : public ofp_stats_request
{
#undef OFCLASS
#define OFCLASS ofp_port_stats_request
    OFBOILERPLATE();
public:
    ofp_port_stats_request()
        : ofp_stats_request(OFPST_PORT), port_no_(0)
    {
        std::fill(pad_, pad_ + sizeof(pad_), '\0');
    }
    ofp_port_stats_request(ofp_stats_request& osr) : ofp_stats_request(osr) {}

    static std::size_t min_bytes() {
        return 16;
    }

    static uint16_t static_type()
    {
        return OFPST_PORT;
    }


private:
    OFDEFMEM(uint16_t, port_no);       /* OFPST_PORT message must request statistics
                                        * either for a single port (specified in
                                        * port_no) or for all ports (if port_no ==
                                        * OFPP_NONE). */
    uint8_t pad_[6];
};

class ofp_queue_stats_request : public ofp_stats_request
{
#undef OFCLASS
#define OFCLASS ofp_queue_stats_request
    OFBOILERPLATE();
public:
    ofp_queue_stats_request()
        : ofp_stats_request(OFPST_QUEUE), port_no_(0), queue_id_(0)
    {
        std::fill(pad_, pad_ + sizeof(pad_), '\0');
    }
    ofp_queue_stats_request(ofp_stats_request& osr) : ofp_stats_request(osr) {}

    static std::size_t min_bytes() {
        return 16;
    }

    static uint16_t static_type()
    {
        return OFPST_QUEUE;
    }


private:
    OFDEFMEM(uint16_t, port_no);       /* All ports if OFPT_ALL. */
    uint8_t pad_[2];                   /* Align to 32-bits. */
    OFDEFMEM(uint32_t, queue_id);      /* All queues if OFPQ_ALL. */
};

class ofp_vendor_stats_request : public ofp_stats_request
{
#undef OFCLASS
#define OFCLASS ofp_vendor_stats_request
    OFBOILERPLATE();
public:
    typedef boost::function<void(ofp_archive_type, ofp_vendor_stats_request*, ofp_vendor_stats_request&)> factory_t;

    ofp_vendor_stats_request(uint32_t vendor_ = OFPVT_INVALID)
        : ofp_stats_request(OFPST_VENDOR) {
        vendor(vendor_);
    }
    ofp_vendor_stats_request(ofp_stats_request& osr) : ofp_stats_request(osr) {}

    static void init();
    static uint16_t static_type()
    {
        return OFPST_VENDOR;
    }

    void factory(ofp_archive_type, ofp_vendor_stats_request*);
    static void register_factory(uint32_t, factory_t);

private:
    typedef boost::unordered_map<uint32_t, factory_t> factory_map_t;
    static factory_map_t factory_map;

    OFDEFMEM(uint32_t, vendor);
};

class ofp_desc_stats_reply : public ofp_stats_reply
{
#undef OFCLASS
#define OFCLASS ofp_desc_stats_reply
    OFBOILERPLATE();
public:
    ofp_desc_stats_reply() : ofp_stats_reply(OFPST_DESC)
    {
        std::fill(mfr_desc_, mfr_desc_ + sizeof(mfr_desc_), '\0');
        std::fill(hw_desc_, hw_desc_ + sizeof(hw_desc_), '\0');
        std::fill(sw_desc_, sw_desc_ + sizeof(sw_desc_), '\0');
        std::fill(serial_num_, serial_num_ + sizeof(serial_num_), '\0');
        std::fill(dp_desc_, dp_desc_ + sizeof(dp_desc_), '\0');
    }
    ofp_desc_stats_reply(ofp_stats_reply& osr) : ofp_stats_reply(osr) {}

    // TODO: FIX THIS
    static std::size_t min_bytes() {
        return 88;
    }

    static uint16_t static_type()
    {
        return OFPST_DESC;
    }


private:
    char mfr_desc_[DESC_STR_LEN];      /* Manufacturer description. */
    char hw_desc_[DESC_STR_LEN];       /* Hardware description. */
    char sw_desc_[DESC_STR_LEN];       /* Software description. */
    char serial_num_[SERIAL_NUM_LEN];  /* Serial number. */
    char dp_desc_[DESC_STR_LEN];       /* Human readable description of datapath. */
};

class ofp_flow_stats_reply : public ofp_stats_reply
{
#undef OFCLASS
#define OFCLASS ofp_flow_stats_reply
    OFBOILERPLATE();
public:
    ofp_flow_stats_reply() : ofp_stats_reply(OFPST_FLOW) {}
    ofp_flow_stats_reply(ofp_stats_reply& osr) : ofp_stats_reply(osr) {}

    static uint16_t static_type()
    {
        return OFPST_FLOW;
    }


private:
    std::vector<ofp_flow_stats> v_;    // OFPST_FLOW
};

class ofp_aggregate_stats_reply : public ofp_stats_reply
{
#undef OFCLASS
#define OFCLASS ofp_aggregate_stats_reply
    OFBOILERPLATE();
public:
    ofp_aggregate_stats_reply()
        : ofp_stats_reply(OFPST_AGGREGATE), packet_count_(0), byte_count_(0), flow_count_(0)
    {
        std::fill(pad_, pad_ + sizeof(pad_), '\0');
    }
    ofp_aggregate_stats_reply(ofp_stats_reply& osr) : ofp_stats_reply(osr) {}

    static std::size_t min_bytes() {
        return 24;
    }

    static uint16_t static_type()
    {
        return OFPST_AGGREGATE;
    }


private:
    OFDEFMEM(uint64_t, packet_count);   /* Number of packets in flows. */
    OFDEFMEM(uint64_t, byte_count);     /* Number of bytes in flows. */
    OFDEFMEM(uint32_t, flow_count);     /* Number of flows. */
    uint8_t pad_[4];                    /* Align to 64 bits. */
};

class ofp_table_stats_reply : public ofp_stats_reply
{
#undef OFCLASS
#define OFCLASS ofp_table_stats_reply
    OFBOILERPLATE();
public:
    ofp_table_stats_reply() : ofp_stats_reply(OFPST_TABLE) {}
    ofp_table_stats_reply(ofp_stats_reply& osr) : ofp_stats_reply(osr) {}

    static uint16_t static_type()
    {
        return OFPST_TABLE;
    }


private:
    std::vector<ofp_table_stats> v_;    // OFPST_TABLE
};

class ofp_port_stats_reply : public ofp_stats_reply
{
#undef OFCLASS
#define OFCLASS ofp_port_stats_reply
    OFBOILERPLATE();
public:
    ofp_port_stats_reply() : ofp_stats_reply(OFPST_PORT) {}
    ofp_port_stats_reply(ofp_stats_reply& osr) : ofp_stats_reply(osr) {}

    static uint16_t static_type()
    {
        return OFPST_PORT;
    }


private:
    std::vector<ofp_port_stats> v_;    // OFPST_PORT
};

class ofp_queue_stats_reply : public ofp_stats_reply
{
#undef OFCLASS
#define OFCLASS ofp_queue_stats_reply
    OFBOILERPLATE();
public:
    ofp_queue_stats_reply() : ofp_stats_reply(OFPST_QUEUE) {}
    ofp_queue_stats_reply(ofp_stats_reply& osr) : ofp_stats_reply(osr) {}

    static uint16_t static_type()
    {
        return OFPST_QUEUE;
    }


private:
    std::vector<ofp_queue_stats> v_;
};

class ofp_vendor_stats_reply : public ofp_stats_reply
{
#undef OFCLASS
#define OFCLASS ofp_vendor_stats_reply
    OFBOILERPLATE();
public:
    typedef boost::function<void(ofp_archive_type, ofp_vendor_stats_reply*, ofp_vendor_stats_reply&)> factory_t;

    ofp_vendor_stats_reply(uint32_t vendor_ = OFPVT_INVALID)
        : ofp_stats_reply(OFPST_VENDOR) {
        vendor(vendor_);
    }
    ofp_vendor_stats_reply(ofp_stats_reply& osr) : ofp_stats_reply(osr) {}

    static void init();
    static uint16_t static_type()
    {
        return OFPST_VENDOR;
    }

    void factory(ofp_archive_type, ofp_vendor_stats_reply*);
    static void register_factory(uint32_t, factory_t);

private:
    typedef boost::unordered_map<uint32_t, factory_t> factory_map_t;
    static factory_map_t factory_map;

    OFDEFMEM(uint32_t, vendor);
};

// 3.6. Send Packet Messages
//    ofp_packet_out

/* Send packet (controller -> datapath). */
class ofp_packet_out : public ofp_msg
{
#undef OFCLASS
#define OFCLASS ofp_packet_out
    OFBOILERPLATE();
public:
    ofp_packet_out()
        : ofp_msg(OFPT_PACKET_OUT, OFP_PACKET_OUT_BYTES),
          actions_len_(0)
    {
        buffer_id(-1);
        in_port(ofp_phy_port::OFPP_NONE),
                packet_buf_ = boost::asio::buffer(packet_, 0);
    }
    ofp_packet_out(ofp_msg& msg) : ofp_msg(msg)
    {
        packet_buf_ =
            boost::asio::buffer(packet_, 0);
    }

    static std::size_t min_bytes() {
        return 16;
    }

    static uint8_t static_type()
    {
        return OFPT_PACKET_OUT;
    }

    const boost::asio::const_buffer& packet() const
    {
        return packet_buf_;
    }
    ofp_packet_out& packet(const boost::asio::const_buffer& packet_buf)
    {
        length(length() + boost::asio::buffer_size(packet_buf) - boost::asio::buffer_size(packet_buf_));
        packet_buf_ = packet_buf;
        return *this;
    }
    ofp_packet_out& add_action(ofp_action* action)
    {
        length(length() + action->len());
        actions_len_ += action->len();
        actions_.push_back(action);
        return *this;
    }

    OFDEFMEM(uint32_t, buffer_id);          /* ID assigned by datapath (-1 if none). */
    OFDEFMEM(uint16_t, in_port);            /* Packet's input port (OFPP_NONE if none). */
    OFDEFMEM(uint16_t, actions_len);        /* Size of action array in bytes. */
    ofp_action_list actions_;               /* Actions. */
    uint8_t packet_[OFP_MAX_PACKET_BYTES];  /* Packet data.  The length is inferred
                                               from the length field in the header.
                                               (Only meaningful if buffer_id == -1.) */
    /* Defined by Amin */
    boost::asio::const_buffer packet_buf_;
};

// 3.7. Barrier Messages
//    ofp_barrier_request, ofp_barrier_reply
class ofp_barrier_request : public ofp_msg
{
#undef OFCLASS
#define OFCLASS ofp_barrier_request
    OFBOILERPLATE();
public:
    ofp_barrier_request() : ofp_msg(OFPT_BARRIER_REQUEST) {}
    ofp_barrier_request(ofp_msg& msg) : ofp_msg(msg) {}

    static uint8_t static_type()
    {
        return OFPT_BARRIER_REQUEST;
    }
};

class ofp_barrier_reply : public ofp_msg
{
#undef OFCLASS
#define OFCLASS ofp_barrier_reply
    OFBOILERPLATE();
public:
    ofp_barrier_reply() : ofp_msg(OFPT_BARRIER_REPLY) {}
    ofp_barrier_reply(ofp_msg& msg) : ofp_msg(msg) {}

    static uint8_t static_type()
    {
        return OFPT_BARRIER_REPLY;
    }
};


//
// 4. Asynchronous Messages
//    ofp_packet_in, ofp_flow_removed, ofp_port_status, ofp_error

/* Packet received on port (datapath -> controller). */
class ofp_packet_in : public ofp_msg
{
#undef OFCLASS
#define OFCLASS ofp_packet_in
    OFBOILERPLATE();
public:
    /* Why is this packet being sent to the controller? */
    enum ofp_packet_in_reason
    {
        OFPR_NO_MATCH,          /* No matching flow. */
        OFPR_ACTION             /* Action explicitly output to controller. */
    };

    ofp_packet_in()
        : ofp_msg(OFPT_PACKET_IN, OFP_PACKET_IN_BYTES), buffer_id_(0),
          total_len_(0), in_port_(0), reason_(0), pad_(0)
    {
        packet_buf_ = boost::asio::buffer(packet_, 0);
    }
    ofp_packet_in(ofp_msg& msg) : ofp_msg(msg)
    {
        packet_buf_ = boost::asio::buffer(packet_, length() - min_bytes());
    }

    static std::size_t min_bytes() {
        return 18;
    }

    static uint8_t static_type()
    {
        return OFPT_PACKET_IN;
    }

    const boost::asio::const_buffer& packet() const
    {
        return packet_buf_;
    }
    ofp_packet_in& packet(const boost::asio::const_buffer& packet_buf)
    {
        length(min_bytes() + boost::asio::buffer_size(packet_buf));
        packet_buf_ = packet_buf;
        return *this;
    }

private:
    OFDEFMEM(uint32_t, buffer_id);    /* ID assigned by datapath. */
    OFDEFMEM(uint16_t, total_len);    /* Full length of frame. */
    OFDEFMEM(uint16_t, in_port);      /* Port on which frame was received. */
    OFDEFMEM(uint8_t, reason);        /* Reason packet is being sent (one of OFPR_*) */
    OFDEFMEM(uint8_t, pad);
    uint8_t packet_[OFP_MAX_PACKET_BYTES];       /* Ethernet frame, halfway through 32-bit word,
                                                    so the IP header is 32-bit aligned.  The
                                                    amount of data is inferred from the length
                                                    field in the header.  Because of padding,
                                                    offsetof(class ofp_packet_in, data) ==
                                                    sizeof(class ofp_packet_in) - 2. */
    /* Defined by Amin */
    boost::asio::const_buffer packet_buf_;
};

/* Flow removed (datapath -> controller). */
class ofp_flow_removed : public ofp_msg
{
#undef OFCLASS
#define OFCLASS ofp_flow_removed
    OFBOILERPLATE();
public:
    /* Why was this flow removed? */
    enum ofp_flow_removed_reason
    {
        OFPRR_IDLE_TIMEOUT,         /* Flow idle time exceeded idle_timeout. */
        OFPRR_HARD_TIMEOUT,         /* Time exceeded hard_timeout. */
        OFPRR_DELETE                /* Evicted by a DELETE flow mod. */
    };

    ofp_flow_removed()
        : ofp_msg(OFPT_FLOW_REMOVED, OFP_FLOW_REMOVED_BYTES), cookie_(0),
          priority_(0), reason_(0), duration_sec_(0), duration_nsec_(0),
          idle_timeout_(0), packet_count_(0), byte_count_(0)
    {
        std::fill(pad_, pad_ + sizeof(pad_), '\0');
        std::fill(pad2_, pad2_ + sizeof(pad2_), '\0');
    }
    ofp_flow_removed(ofp_msg& msg) : ofp_msg(msg) {}

    static std::size_t min_bytes() {
        return 88;
    }

    static uint8_t static_type()
    {
        return OFPT_FLOW_REMOVED;
    }


private:
    ofp_match match_;                   /* Description of fields. */
    OFDEFMEM(uint64_t, cookie);         /* Opaque controller-issued identifier. */

    OFDEFMEM(uint16_t, priority);       /* Priority level of flow entry. */
    OFDEFMEM(uint8_t, reason);          /* One of OFPRR_*. */
    uint8_t pad_[1];                    /* Align to 32-bits. */

    OFDEFMEM(uint32_t, duration_sec);   /* Time flow was alive in seconds. */
    OFDEFMEM(uint32_t, duration_nsec);  /* Time flow was alive in nanoseconds beyond
                                           duration_sec. */
    OFDEFMEM(uint16_t, idle_timeout);   /* Idle timeout from original flow mod. */
    uint8_t pad2_[2];                   /* Align to 64-bits. */
    OFDEFMEM(uint64_t, packet_count);
    OFDEFMEM(uint64_t, byte_count);
};

/* A physical port has changed in the datapath */
class ofp_port_status : public ofp_msg
{
#undef OFCLASS
#define OFCLASS ofp_port_status
    OFBOILERPLATE();
public:
    /* What changed about the physical port */
    enum ofp_port_reason
    {
        OFPPR_ADD,              /* The port was added. */
        OFPPR_DELETE,           /* The port was removed. */
        OFPPR_MODIFY            /* Some attribute of the port has changed. */
    };

    ofp_port_status()
        : ofp_msg(OFPT_PORT_STATUS, OFP_PORT_STATUS_BYTES), reason_(0)
    {
        std::fill(pad_, pad_ + sizeof(pad_), '\0');
    }
    ofp_port_status(ofp_msg& msg) : ofp_msg(msg) {}

    static std::size_t min_bytes() {
        return 64;
    }

    static uint8_t static_type()
    {
        return OFPT_PORT_STATUS;
    }


private:
    OFDEFMEM(uint8_t, reason);         /* One of OFPPR_*. */
    uint8_t pad_[7];                   /* Align to 64-bits. */
    OFDEFMEM(ofp_phy_port, desc);
};

/* OFPT_ERROR: Error message (datapath -> controller). */
class ofp_error_msg : public ofp_msg
{
#undef OFCLASS
#define OFCLASS ofp_error_msg
    OFBOILERPLATE();
public:
    enum ofp_error_type
    {
        OFPET_HELLO_FAILED,         /* Hello protocol failed. */
        OFPET_BAD_REQUEST,          /* Request was not understood. */
        OFPET_BAD_ACTION,           /* Error in action description. */
        OFPET_FLOW_MOD_FAILED,      /* Problem modifying flow entry. */
        OFPET_PORT_MOD_FAILED,      /* Port mod request failed. */
        OFPET_QUEUE_OP_FAILED       /* Queue operation failed. */
    };

    /* ofp_error_msg 'code' values for OFPET_HELLO_FAILED.  'data' contains an
     * ASCII text string that may give failure details. */
    enum ofp_hello_failed_code
    {
        OFPHFC_INCOMPATIBLE,        /* No compatible version. */
        OFPHFC_EPERM                /* Permissions error. */
    };

    /* ofp_error_msg 'code' values for OFPET_BAD_REQUEST.  'data' contains at least
     * the first 64 bytes of the failed request. */
    enum ofp_bad_request_code
    {
        OFPBRC_BAD_VERSION,         /* ofp_header.version not supported. */
        OFPBRC_BAD_TYPE,            /* ofp_header.type not supported. */
        OFPBRC_BAD_STAT,            /* ofp_stats_request.type not supported. */
        OFPBRC_BAD_VENDOR,          /* Vendor not supported (in ofp_vendor_header
                                     * or ofp_stats_request or ofp_stats_reply). */
        OFPBRC_BAD_SUBTYPE,         /* Vendor subtype not supported. */
        OFPBRC_EPERM,               /* Permissions error. */
        OFPBRC_BAD_LEN,             /* Wrong request length for type. */
        OFPBRC_BUFFER_EMPTY,        /* Specified buffer has already been used. */
        OFPBRC_BUFFER_UNKNOWN       /* Specified buffer does not exist. */
    };

    /* ofp_error_msg 'code' values for OFPET_BAD_ACTION.  'data' contains at least
     * the first 64 bytes of the failed request. */
    enum ofp_bad_action_code
    {
        OFPBAC_BAD_TYPE,           /* Unknown action type. */
        OFPBAC_BAD_LEN,            /* Length problem in actions. */
        OFPBAC_BAD_VENDOR,         /* Unknown vendor id specified. */
        OFPBAC_BAD_VENDOR_TYPE,    /* Unknown action type for vendor id. */
        OFPBAC_BAD_OUT_PORT,       /* Problem validating output action. */
        OFPBAC_BAD_ARGUMENT,       /* Bad action argument. */
        OFPBAC_EPERM,              /* Permissions error. */
        OFPBAC_TOO_MANY,           /* Can't handle this many actions. */
        OFPBAC_BAD_QUEUE           /* Problem validating output queue. */
    };

    /* ofp_error_msg 'code' values for OFPET_FLOW_MOD_FAILED.  'data' contains
     * at least the first 64 bytes of the failed request. */
    enum ofp_flow_mod_failed_code
    {
        OFPFMFC_ALL_TABLES_FULL,    /* Flow not added because of full tables. */
        OFPFMFC_OVERLAP,            /* Attempted to add overlapping flow with
                                     * CHECK_OVERLAP flag set. */
        OFPFMFC_EPERM,              /* Permissions error. */
        OFPFMFC_BAD_EMERG_TIMEOUT,  /* Flow not added because of non-zero idle/hard
                                     * timeout. */
        OFPFMFC_BAD_COMMAND,        /* Unknown command. */
        OFPFMFC_UNSUPPORTED         /* Unsupported action list - cannot process in
                                     * the order specified. */
    };

    /* ofp_error_msg 'code' values for OFPET_PORT_MOD_FAILED.  'data' contains
     * at least the first 64 bytes of the failed request. */
    enum ofp_port_mod_failed_code
    {
        OFPPMFC_BAD_PORT,            /* Specified port does not exist. */
        OFPPMFC_BAD_HW_ADDR,         /* Specified hardware address is wrong. */
    };

    /* ofp_error msg 'code' values for OFPET_QUEUE_OP_FAILED. 'data' contains
     * at least the first 64 bytes of the failed request */
    enum ofp_queue_op_failed_code
    {
        OFPQOFC_BAD_PORT,           /* Invalid port (or port does not exist). */
        OFPQOFC_BAD_QUEUE,          /* Queue does not exist. */
        OFPQOFC_EPERM               /* Permissions error. */
    };

    ofp_error_msg() :
        ofp_msg(OFPT_ERROR, OFP_ERROR_MSG_BYTES), type_(0), code_(0) {}
    ofp_error_msg(ofp_msg& msg) : ofp_msg(msg) {}

    static std::size_t min_bytes() {
        return 12;
    }

    static uint8_t static_type()
    {
        return OFPT_ERROR;
    }


private:
    OFDEFMEM(uint16_t, type);
    OFDEFMEM(uint16_t, code);
    uint8_t data_[OFP_MAX_MSG_BYTES]; /* Variable-length data.  Interpreted based
                                   on the type and code. */
    /* Defined by Amin */
    std::size_t data_size_;
};


//
// 5. Symmetric Messages
//    ofp_hello, ofp_echo_request, ofp_echo_reply,
//    ofp_vendor, ofp_features_request,
//    ofp_get_config_request, ofp_get_config_reply, ofp_set_config

/* OFPT_HELLO.  This message has an empty body, but implementations must
 * ignore any data included in the body, to allow for future extensions. */
class ofp_hello : public ofp_msg
{
#undef OFCLASS
#define OFCLASS ofp_hello
    OFBOILERPLATE();
public:
    ofp_hello() : ofp_msg(OFPT_HELLO, OFP_HELLO_BYTES) {}
    ofp_hello(ofp_msg& msg) : ofp_msg(msg) {}

    static uint8_t static_type()
    {
        return OFPT_HELLO;
    }
};

class ofp_echo_request : public ofp_msg
{
#undef OFCLASS
#define OFCLASS ofp_echo_request
    OFBOILERPLATE();
public:
    ofp_echo_request() : ofp_msg(OFPT_ECHO_REQUEST) {}
    ofp_echo_request(ofp_msg& msg) : ofp_msg(msg) {}

    static uint8_t static_type()
    {
        return OFPT_ECHO_REQUEST;
    }
    const boost::asio::const_buffer& payload() const
    {
        return payload_buf_;
    }

private:
    uint8_t payload_[OFP_MAX_PACKET_BYTES];
    /* Defined by Amin */
    boost::asio::const_buffer payload_buf_;

};

class ofp_echo_reply : public ofp_msg
{
#undef OFCLASS
#define OFCLASS ofp_echo_reply
    OFBOILERPLATE();
public:
    ofp_echo_reply() : ofp_msg(OFPT_ECHO_REPLY) {}
    ofp_echo_reply(ofp_msg& msg) : ofp_msg(msg) {}
    ofp_echo_reply(const ofp_echo_request& req)
        : ofp_msg(OFPT_ECHO_REPLY, req.length(), req.xid()),
          payload_buf_(req.payload())
    {}

    static uint8_t static_type()
    {
        return OFPT_ECHO_REPLY;
    }
private:
    /* Defined by Amin */
    // TODO: assumes it always points to the request payload
    boost::asio::const_buffer payload_buf_;
};

/* Vendor extension. */
class ofp_vendor : public ofp_msg
{
#undef OFCLASS
#define OFCLASS ofp_vendor
    OFBOILERPLATE();
public:
    typedef boost::function<void(ofp_archive_type, ofp_vendor*, ofp_vendor&)> factory_t;

    ofp_vendor(uint32_t vendor_ = OFPVT_INVALID)
        : ofp_msg(OFPT_VENDOR, OFP_VENDOR_HEADER_BYTES)
    {
        vendor(vendor_);
    }
    ofp_vendor(ofp_msg& msg) : ofp_msg(msg) {}

    static std::size_t min_bytes() {
        return 12;
    }

    static void init();
    static uint8_t static_type()
    {
        return OFPT_VENDOR;
    }

    void factory(ofp_archive_type, ofp_vendor*);
    static void register_factory(uint32_t, factory_t);

    OFDEFMEM(uint32_t, vendor);
private:
    typedef boost::unordered_map<uint32_t, factory_t> factory_map_t;
    static factory_map_t factory_map;
};

class ofp_features_request : public ofp_msg
{
#undef OFCLASS
#define OFCLASS ofp_features_request
    OFBOILERPLATE();
public:
    ofp_features_request()
        : ofp_msg(OFPT_FEATURES_REQUEST, OFP_FEATURES_REQUEST_BYTES) {}
    ofp_features_request(ofp_msg& msg) : ofp_msg(msg) {}

    static uint8_t static_type()
    {
        return OFPT_FEATURES_REQUEST;
    }
};

class ofp_get_config_request : public ofp_msg
{
#undef OFCLASS
#define OFCLASS ofp_get_config_request
    OFBOILERPLATE();
public:
    ofp_get_config_request()
        : ofp_msg(OFPT_GET_CONFIG_REQUEST) {}
    ofp_get_config_request(ofp_msg& msg) : ofp_msg(msg) {}

    static uint8_t static_type()
    {
        return OFPT_GET_CONFIG_REQUEST;
    }
};

class ofp_get_config_reply : public ofp_switch_config
{
#undef OFCLASS
#define OFCLASS ofp_get_config_reply
    OFBOILERPLATE();
public:
    ofp_get_config_reply() : ofp_switch_config(OFPT_GET_CONFIG_REPLY) {}
    ofp_get_config_reply(ofp_msg& msg) : ofp_switch_config(msg) {}

    static uint8_t static_type()
    {
        return OFPT_GET_CONFIG_REPLY;
    }
};

class ofp_set_config : public ofp_switch_config
{
#undef OFCLASS
#define OFCLASS ofp_set_config
    OFBOILERPLATE();
public:
    ofp_set_config() : ofp_switch_config(OFPT_SET_CONFIG) {}
    ofp_set_config(ofp_msg& msg) : ofp_switch_config(msg) {}

    static uint8_t static_type()
    {
        return OFPT_SET_CONFIG;
    }
};

#undef OFDEFMEM

} // namespace v1
} // namespace openflow
} // namespace vigil

#include <openflow/openflow-inl-1.0.hh>

#endif /* openflow-1.0.hh */
