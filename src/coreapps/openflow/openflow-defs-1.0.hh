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

#ifndef OPENFLOW_OF1_DEFS_H
#define OPENFLOW_OF1_DEFS_H

#include <byteswap.h>
#include <stdint.h>

namespace vigil
{
namespace openflow
{
namespace v1
{

/* Version number:
 * Non-experimental versions released: 0x01
 * Experimental versions released: 0x81 -- 0x99
 * 
 * The most significant bit being set in the version field indicates an
 * experimental OpenFlow version.
 */
const unsigned int OFP_VERSION = 0x01;

const unsigned int OFP_MAX_TABLE_NAME_LEN = 32;
const unsigned int OFP_MAX_PORT_NAME_LEN = 16;

const unsigned int OFP_TCP_PORT = 6633;
const unsigned int OFP_SSL_PORT = 6633;

const unsigned int OFP_ETH_ALEN = 6;          /* Bytes in an Ethernet address. */

/* The VLAN id is 12 bits, so we can use the entire 16 bits to indicate
 * special conditions.  All ones is used to match that no VLAN id was
 * set. */
const unsigned int OFP_VLAN_NONE = 0xffff;

const unsigned int OFP_DEFAULT_MISS_SEND_LEN = 128;

/* Values below this cutoff are 802.3 packets and the two bytes
 * following MAC addresses are used as a frame length.  Otherwise, the
 * two bytes are used as the Ethernet type.
 */
const unsigned int OFP_DL_TYPE_ETH2_CUTOFF = 0x0600;

/* Value of dl_type to indicate that the frame does not include an
 * Ethernet type.
 */
const unsigned int OFP_DL_TYPE_NOT_ETH_TYPE = 0x05ff;

/* Value used in "idle_timeout" and "hard_timeout" to indicate that the entry
 * is permanent. */
const unsigned int OFP_FLOW_PERMANENT = 0;

/* By default, choose a priority in the middle. */
const unsigned int OFP_DEFAULT_PRIORITY = 0x8000;

const unsigned int DESC_STR_LEN = 256;
const unsigned int SERIAL_NUM_LEN = 32;

/* All ones is used to indicate all queues in a port (for stats retrieval). */
const unsigned int OFPQ_ALL = 0xffffffff;

/* Min rate > 1000 means not configured. */
const unsigned int OFPQ_MIN_RATE_UNCFG = 0xffff;

const unsigned int OFP_ACTION_DL_ADDR_BYTES = 16;
const unsigned int OFP_ACTION_ENQUEUE_BYTES = 16;
const unsigned int OFP_ACTION_HEADER_BYTES = 8;
const unsigned int OFP_ACTION_NW_ADDR_BYTES = 8;
const unsigned int OFP_ACTION_NW_TOS_BYTES = 8;
const unsigned int OFP_ACTION_OUTPUT_BYTES = 8;
const unsigned int OFP_ACTION_TP_PORT_BYTES = 8;
const unsigned int OFP_ACTION_VENDOR_HEADER_BYTES = 8;
const unsigned int OFP_ACTION_VLAN_PCP_BYTES = 8;
const unsigned int OFP_ACTION_VLAN_VID_BYTES = 8;
const unsigned int OFP_AGGREGATE_STATS_REPLY_BYTES = 24;
const unsigned int OFP_AGGREGATE_STATS_REQUEST_BYTES = 44;
const unsigned int OFP_DESC_STATS_BYTES = 1056;
const unsigned int OFP_ERROR_MSG_BYTES = 12;
const unsigned int OFP_FEATURES_REQUEST_BYTES = 8;
const unsigned int OFP_FEATURES_REPLY_BYTES = 32;
const unsigned int OFP_FLOW_MOD_BYTES = 72;
const unsigned int OFP_FLOW_REMOVED_BYTES = 88;
const unsigned int OFP_FLOW_STATS_BYTES = 88;
const unsigned int OFP_FLOW_STATS_REQUEST_BYTES = 44;
const unsigned int OFP_HEADER_BYTES = 8;
const unsigned int OFP_HELLO_BYTES = 8;
const unsigned int OFP_MATCH_BYTES = 40;
const unsigned int OFP_PACKET_IN_BYTES = 18;
const unsigned int OFP_PACKET_OUT_BYTES = 16;
const unsigned int OFP_PACKET_QUEUE_BYTES = 8;
const unsigned int OFP_PHY_PORT_BYTES = 48;
const unsigned int OFP_PORT_MOD_BYTES = 32;
const unsigned int OFP_PORT_STATS_BYTES = 104;
const unsigned int OFP_PORT_STATS_REQUEST_BYTES = 8;
const unsigned int OFP_PORT_STATUS_BYTES = 64;
const unsigned int OFP_QUEUE_GET_CONFIG_REPLY_BYTES = 16;
const unsigned int OFP_QUEUE_GET_CONFIG_REQUEST_BYTES = 12;
const unsigned int OFP_QUEUE_PROP_HEADER_BYTES = 8;
const unsigned int OFP_QUEUE_PROP_MIN_RATE_BYTES = 16;
const unsigned int OFP_QUEUE_STATS_BYTES = 32;
const unsigned int OFP_QUEUE_STATS_REQUEST_BYTES = 8;
const unsigned int OFP_STATS_REPLY_BYTES = 12;
const unsigned int OFP_STATS_REQUEST_BYTES = 12;
const unsigned int OFP_SWITCH_CONFIG_BYTES = 12;
const unsigned int OFP_TABLE_STATS_BYTES = 64;
const unsigned int OFP_VENDOR_HEADER_BYTES = 12;


// TODO: By Amin
const unsigned int OFP_MAX_XID = 0x7FFFFFFF;
const unsigned int OFP_MAX_LEN = UINT16_MAX;
const unsigned int OFP_MAX_MSG_BYTES = 64 * 1024;
const unsigned int OFP_MAX_ACTION_BYTES = UINT8_MAX;
const unsigned int OFP_MAX_ACTION_COUNT = 64;
const unsigned int OFP_MAX_PACKET_BYTES = 9 * 1024 + 128;
const unsigned int OFP_MAX_QUEUES = 1024;
const unsigned int OFP_MAX_QUEUE_PROP_BYTES = 64;
const unsigned int OFP_MAX_QUEUE_PROP_COUNT = 64;
const unsigned int OFP_MAX_STATS_PER_REPLY = 64;

// TODO: imported -- fix
/** Send flow removed messages
 */
#define SEND_FLOW_REMOVED true

/** Default idle timeout for flows
 */
#define DEFAULT_FLOW_TIMEOUT 5

/* Flow wildcards. */
enum ofp_flow_wildcards
{
    OFPFW_IN_PORT  = 1 << 0,  /* Switch input port. */
    OFPFW_DL_VLAN  = 1 << 1,  /* VLAN id. */
    OFPFW_DL_SRC   = 1 << 2,  /* Ethernet source address. */
    OFPFW_DL_DST   = 1 << 3,  /* Ethernet destination address. */
    OFPFW_DL_TYPE  = 1 << 4,  /* Ethernet frame type. */
    OFPFW_NW_PROTO = 1 << 5,  /* IP protocol. */
    OFPFW_TP_SRC   = 1 << 6,  /* TCP/UDP source port. */
    OFPFW_TP_DST   = 1 << 7,  /* TCP/UDP destination port. */

    /* IP source address wildcard bit count.  0 is exact match, 1 ignores the
     * LSB, 2 ignores the 2 least-significant bits, ..., 32 and higher wildcard
     * the entire field.  This is the *opposite* of the usual convention where
     * e.g. /24 indicates that 8 bits (not 24 bits) are wildcarded. */
    OFPFW_NW_SRC_SHIFT = 8,
    OFPFW_NW_SRC_BITS = 6,
    OFPFW_NW_SRC_MASK = ((1 << OFPFW_NW_SRC_BITS) - 1) << OFPFW_NW_SRC_SHIFT,
    OFPFW_NW_SRC_ALL = 32 << OFPFW_NW_SRC_SHIFT,

    /* IP destination address wildcard bit count.  Same format as source. */
    OFPFW_NW_DST_SHIFT = 14,
    OFPFW_NW_DST_BITS = 6,
    OFPFW_NW_DST_MASK = ((1 << OFPFW_NW_DST_BITS) - 1) << OFPFW_NW_DST_SHIFT,
    OFPFW_NW_DST_ALL = 32 << OFPFW_NW_DST_SHIFT,

    OFPFW_DL_VLAN_PCP = 1 << 20,  /* VLAN priority. */
    OFPFW_NW_TOS = 1 << 21,  /* IP ToS (DSCP field, 6 bits). */

    /* Wildcard all fields. */
    OFPFW_ALL = ((1 << 22) - 1)
};

/* The wildcards for ICMP type and code fields use the transport source
 * and destination port fields, respectively. */
const unsigned int OFPFW_ICMP_TYPE = OFPFW_TP_SRC;
const unsigned int OFPFW_ICMP_CODE = OFPFW_TP_DST;

enum ofp_vendor_type
{
	OFPVT_INVALID = 0xffffffff
};

template <class T>
inline T& swap_bytes(T& t){
	return t;
}

inline uint16_t swap_bytes(const uint16_t & t)
{
	return bswap_16(t);
}

inline uint32_t swap_bytes(const uint32_t & t)
{
	return bswap_32(t);
}

inline uint64_t swap_bytes(const uint64_t & t)
{
	return bswap_64(t);
}

} // namespace v1
} // namespace openflow
} // namespace vigil

#endif /* openflow-defs-1.0.hh */

