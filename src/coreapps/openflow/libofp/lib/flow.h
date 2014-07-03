/*
 * Copyright (c) 2008, 2009, 2010, 2011, 2012, 2013 Nicira, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef FLOW_H
#define FLOW_H 1

#include <sys/types.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "openflow/nicira-ext.h"
#include "openflow/openflow.h"
#include "hash.h"
#include "util.h"

struct dpif_flow_stats;
struct ds;
struct flow_wildcards;
struct miniflow;
struct minimask;
struct ofpbuf;

#ifdef  __cplusplus
extern "C" {
#endif

/* This sequence number should be incremented whenever anything involving flows
 * or the wildcarding of flows changes.  This will cause build assertion
 * failures in places which likely need to be updated. */
#define FLOW_WC_SEQ 20

#define FLOW_N_REGS 8
BUILD_ASSERT_DECL(FLOW_N_REGS <= NXM_NX_MAX_REGS);

/* Used for struct flow's dl_type member for frames that have no Ethernet
 * type, that is, pure 802.2 frames. */
#define FLOW_DL_TYPE_NONE 0x5ff

/* Fragment bits, used for IPv4 and IPv6, always zero for non-IP flows. */
#define FLOW_NW_FRAG_ANY   (1 << 0) /* Set for any IP frag. */
#define FLOW_NW_FRAG_LATER (1 << 1) /* Set for IP frag with nonzero offset. */
#define FLOW_NW_FRAG_MASK  (FLOW_NW_FRAG_ANY | FLOW_NW_FRAG_LATER)

BUILD_ASSERT_DECL(FLOW_NW_FRAG_ANY == NX_IP_FRAG_ANY);
BUILD_ASSERT_DECL(FLOW_NW_FRAG_LATER == NX_IP_FRAG_LATER);

#define FLOW_TNL_F_DONT_FRAGMENT (1 << 0)
#define FLOW_TNL_F_CSUM (1 << 1)
#define FLOW_TNL_F_KEY (1 << 2)

const char *flow_tun_flag_to_string(uint32_t flags);

struct flow_tnl {
    ovs_be64 tun_id;
    ovs_be32 ip_src;
    ovs_be32 ip_dst;
    uint16_t flags;
    uint8_t ip_tos;
    uint8_t ip_ttl;
};

/* Unfortunately, a "struct flow" sometimes has to handle OpenFlow port
 * numbers and other times datapath (dpif) port numbers.  This union allows
 * access to both. */
union flow_in_port {
    ofp_port_t ofp_port;
    odp_port_t odp_port;
};

/*
* A flow in the network.
*
* The meaning of 'in_port' is context-dependent.  In most cases, it is a
* 16-bit OpenFlow 1.0 port number.  In the software datapath interface (dpif)
* layer and its implementations (e.g. dpif-linux, dpif-netdev), it is instead
* a 32-bit datapath port number.
*/
struct flow {
    struct flow_tnl tunnel;     /* Encapsulating tunnel parameters. */
    ovs_be64 metadata;          /* OpenFlow Metadata. */
    struct in6_addr ipv6_src;   /* IPv6 source address. */
    struct in6_addr ipv6_dst;   /* IPv6 destination address. */
    struct in6_addr nd_target;  /* IPv6 neighbor discovery (ND) target. */
    uint32_t skb_priority;      /* Packet priority for QoS. */
    uint32_t regs[FLOW_N_REGS]; /* Registers. */
    ovs_be32 nw_src;            /* IPv4 source address. */
    ovs_be32 nw_dst;            /* IPv4 destination address. */
    ovs_be32 ipv6_label;        /* IPv6 flow label. */
    union flow_in_port in_port; /* Input port.*/
    uint32_t pkt_mark;          /* Packet mark. */
    ovs_be32 mpls_lse;          /* MPLS label stack entry. */
    uint16_t mpls_depth;        /* Depth of MPLS stack. */
    ovs_be16 vlan_tci;          /* If 802.1Q, TCI | VLAN_CFI; otherwise 0. */
    ovs_be16 dl_type;           /* Ethernet frame type. */
    ovs_be16 tp_src;            /* TCP/UDP/SCTP source port. */
    ovs_be16 tp_dst;            /* TCP/UDP/SCTP destination port. */
    uint8_t dl_src[6];          /* Ethernet source address. */
    uint8_t dl_dst[6];          /* Ethernet destination address. */
    uint8_t nw_proto;           /* IP protocol or low 8 bits of ARP opcode. */
    uint8_t nw_tos;             /* IP ToS (including DSCP and ECN). */
    uint8_t arp_sha[6];         /* ARP/ND source hardware address. */
    uint8_t arp_tha[6];         /* ARP/ND target hardware address. */
    uint8_t nw_ttl;             /* IP TTL/Hop Limit. */
    uint8_t nw_frag;            /* FLOW_FRAG_* flags. */
    uint8_t zeros[6];
};
BUILD_ASSERT_DECL(sizeof(struct flow) % 4 == 0);

#define FLOW_U32S (sizeof(struct flow) / 4)

/* Remember to update FLOW_WC_SEQ when changing 'struct flow'. */
BUILD_ASSERT_DECL(sizeof(struct flow) == sizeof(struct flow_tnl) + 160 &&
                  FLOW_WC_SEQ == 20);

/* Represents the metadata fields of struct flow. */
struct flow_metadata {
    ovs_be64 tun_id;                 /* Encapsulating tunnel ID. */
    ovs_be32 tun_src;                /* Tunnel outer IPv4 src addr */
    ovs_be32 tun_dst;                /* Tunnel outer IPv4 dst addr */
    ovs_be64 metadata;               /* OpenFlow 1.1+ metadata field. */
    uint32_t regs[FLOW_N_REGS];      /* Registers. */
    uint32_t pkt_mark;               /* Packet mark. */
    ofp_port_t in_port;              /* OpenFlow port or zero. */
};

void flow_extract(struct ofpbuf *, uint32_t priority, uint32_t mark,
                  const struct flow_tnl *, const union flow_in_port *in_port,
                  struct flow *);

void flow_zero_wildcards(struct flow *, const struct flow_wildcards *);
void flow_get_metadata(const struct flow *, struct flow_metadata *);

char *flow_to_string(const struct flow *);
void format_flags(struct ds *ds, const char *(*bit_to_string)(uint32_t),
                  uint32_t flags, char del);

void flow_format(struct ds *, const struct flow *);
void flow_print(FILE *, const struct flow *);
static inline int flow_compare_3way(const struct flow *, const struct flow *);
static inline bool flow_equal(const struct flow *, const struct flow *);
static inline size_t flow_hash(const struct flow *, uint32_t basis);

void flow_set_dl_vlan(struct flow *, ovs_be16 vid);
void flow_set_vlan_vid(struct flow *, ovs_be16 vid);
void flow_set_vlan_pcp(struct flow *, uint8_t pcp);

void flow_set_mpls_label(struct flow *flow, ovs_be32 label);
void flow_set_mpls_ttl(struct flow *flow, uint8_t ttl);
void flow_set_mpls_tc(struct flow *flow, uint8_t tc);
void flow_set_mpls_bos(struct flow *flow, uint8_t stack);

void flow_compose(struct ofpbuf *, const struct flow *);

static inline int
flow_compare_3way(const struct flow *a, const struct flow *b)
{
    return memcmp(a, b, sizeof *a);
}

static inline bool
flow_equal(const struct flow *a, const struct flow *b)
{
    return !flow_compare_3way(a, b);
}

static inline size_t
flow_hash(const struct flow *flow, uint32_t basis)
{
    return hash_words((const uint32_t *) flow, sizeof *flow / 4, basis);
}

static inline uint16_t
ofp_to_u16(ofp_port_t ofp_port)
{
    return (OVS_FORCE uint16_t) ofp_port;
}

static inline uint32_t
odp_to_u32(odp_port_t odp_port)
{
    return (OVS_FORCE uint32_t) odp_port;
}

static inline uint32_t
ofp11_to_u32(ofp11_port_t ofp11_port)
{
    return (OVS_FORCE uint32_t) ofp11_port;
}

static inline ofp_port_t
u16_to_ofp(uint16_t port)
{
    return OFP_PORT_C(port);
}

static inline odp_port_t
u32_to_odp(uint32_t port)
{
    return ODP_PORT_C(port);
}

static inline ofp11_port_t
u32_to_ofp11(uint32_t port)
{
    return OFP11_PORT_C(port);
}

static inline uint32_t
hash_ofp_port(ofp_port_t ofp_port)
{
    return hash_int(ofp_to_u16(ofp_port), 0);
}

static inline uint32_t
hash_odp_port(odp_port_t odp_port)
{
    return hash_int(odp_to_u32(odp_port), 0);
}

uint32_t flow_hash_in_minimask(const struct flow *, const struct minimask *,
                               uint32_t basis);

/* Wildcards for a flow.
 *
 * A 1-bit in each bit in 'masks' indicates that the corresponding bit of
 * the flow is significant (must match).  A 0-bit indicates that the
 * corresponding bit of the flow is wildcarded (need not match). */
struct flow_wildcards {
    struct flow masks;
};

void flow_wildcards_init_catchall(struct flow_wildcards *);
void flow_wildcards_init_exact(struct flow_wildcards *);

bool flow_wildcards_is_catchall(const struct flow_wildcards *);

void flow_wildcards_set_reg_mask(struct flow_wildcards *,
                                 int idx, uint32_t mask);

void flow_wildcards_and(struct flow_wildcards *dst,
                        const struct flow_wildcards *src1,
                        const struct flow_wildcards *src2);
void flow_wildcards_or(struct flow_wildcards *dst,
                       const struct flow_wildcards *src1,
                       const struct flow_wildcards *src2);
bool flow_wildcards_has_extra(const struct flow_wildcards *,
                              const struct flow_wildcards *);

void flow_wildcards_fold_minimask(struct flow_wildcards *,
                                  const struct minimask *);

uint32_t flow_wildcards_hash(const struct flow_wildcards *, uint32_t basis);
bool flow_wildcards_equal(const struct flow_wildcards *,
                          const struct flow_wildcards *);
uint32_t flow_hash_symmetric_l4(const struct flow *flow, uint32_t basis);

void flow_mask_hash_fields(const struct flow *, struct flow_wildcards *,
                           enum nx_hash_fields);
uint32_t flow_hash_fields(const struct flow *, enum nx_hash_fields,
                          uint16_t basis);
const char *flow_hash_fields_to_str(enum nx_hash_fields);
bool flow_hash_fields_valid(enum nx_hash_fields);

uint32_t flow_hash_in_wildcards(const struct flow *,
                                const struct flow_wildcards *,
                                uint32_t basis);

bool flow_equal_except(const struct flow *a, const struct flow *b,
                       const struct flow_wildcards *);

/* Compressed flow. */

#define MINI_N_INLINE (sizeof(void *) == 4 ? 7 : 8)
#define MINI_N_MAPS DIV_ROUND_UP(FLOW_U32S, 32)

/* A sparse representation of a "struct flow".
 *
 * A "struct flow" is fairly large and tends to be mostly zeros.  Sparse
 * representation has two advantages.  First, it saves memory.  Second, it
 * saves time when the goal is to iterate over only the nonzero parts of the
 * struct.
 *
 * The 'map' member holds one bit for each uint32_t in a "struct flow".  Each
 * 0-bit indicates that the corresponding uint32_t is zero, each 1-bit that it
 * is nonzero.
 *
 * 'values' points to the start of an array that has one element for each 1-bit
 * in 'map'.  The least-numbered 1-bit is in values[0], the next 1-bit is in
 * values[1], and so on.
 *
 * 'values' may point to a few different locations:
 *
 *     - If 'map' has MINI_N_INLINE or fewer 1-bits, it may point to
 *       'inline_values'.  One hopes that this is the common case.
 *
 *     - If 'map' has more than MINI_N_INLINE 1-bits, it may point to memory
 *       allocated with malloc().
 *
 *     - The caller could provide storage on the stack for situations where
 *       that makes sense.  So far that's only proved useful for
 *       minimask_combine(), but the principle works elsewhere.
 *
 * The implementation maintains and depends on the invariant that every element
 * in 'values' is nonzero; that is, wherever a 1-bit appears in 'map', the
 * corresponding element of 'values' must be nonzero.
 */
struct miniflow {
    uint32_t *values;
    uint32_t inline_values[MINI_N_INLINE];
    uint32_t map[MINI_N_MAPS];
};

void miniflow_init(struct miniflow *, const struct flow *);
void miniflow_clone(struct miniflow *, const struct miniflow *);
void miniflow_move(struct miniflow *dst, struct miniflow *);
void miniflow_destroy(struct miniflow *);

void miniflow_expand(const struct miniflow *, struct flow *);

uint32_t miniflow_get(const struct miniflow *, unsigned int u32_ofs);
uint16_t miniflow_get_vid(const struct miniflow *);

bool miniflow_equal(const struct miniflow *a, const struct miniflow *b);
bool miniflow_equal_in_minimask(const struct miniflow *a,
                                const struct miniflow *b,
                                const struct minimask *);
bool miniflow_equal_flow_in_minimask(const struct miniflow *a,
                                     const struct flow *b,
                                     const struct minimask *);
uint32_t miniflow_hash(const struct miniflow *, uint32_t basis);
uint32_t miniflow_hash_in_minimask(const struct miniflow *,
                                   const struct minimask *, uint32_t basis);

/* Compressed flow wildcards. */

/* A sparse representation of a "struct flow_wildcards".
 *
 * See the large comment on struct miniflow for details. */
struct minimask {
    struct miniflow masks;
};

void minimask_init(struct minimask *, const struct flow_wildcards *);
void minimask_clone(struct minimask *, const struct minimask *);
void minimask_move(struct minimask *dst, struct minimask *src);
void minimask_combine(struct minimask *dst,
                      const struct minimask *a, const struct minimask *b,
                      uint32_t storage[FLOW_U32S]);
void minimask_destroy(struct minimask *);

void minimask_expand(const struct minimask *, struct flow_wildcards *);

uint32_t minimask_get(const struct minimask *, unsigned int u32_ofs);
uint16_t minimask_get_vid_mask(const struct minimask *);

bool minimask_equal(const struct minimask *a, const struct minimask *b);
uint32_t minimask_hash(const struct minimask *, uint32_t basis);

bool minimask_has_extra(const struct minimask *, const struct minimask *);
bool minimask_is_catchall(const struct minimask *);
#ifdef  __cplusplus
}
#endif

#endif /* flow.h */
