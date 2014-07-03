/*
 * Copyright (c) 2009, 2010, 2011, 2012, 2013 Nicira, Inc.
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

#include <config.h>

#include "ofproto/ofproto-dpif.h"
#include "ofproto/ofproto-provider.h"

#include <errno.h>

#include "bfd.h"
#include "bond.h"
#include "bundle.h"
#include "byte-order.h"
#include "connmgr.h"
#include "coverage.h"
#include "cfm.h"
#include "dpif.h"
#include "dynamic-string.h"
#include "fail-open.h"
#include "guarded-list.h"
#include "hmapx.h"
#include "lacp.h"
#include "learn.h"
#include "mac-learning.h"
#include "meta-flow.h"
#include "multipath.h"
#include "netdev-vport.h"
#include "netdev.h"
#include "netlink.h"
#include "nx-match.h"
#include "odp-util.h"
#include "odp-execute.h"
#include "ofp-util.h"
#include "ofpbuf.h"
#include "ofp-actions.h"
#include "ofp-parse.h"
#include "ofp-print.h"
#include "ofproto-dpif-governor.h"
#include "ofproto-dpif-ipfix.h"
#include "ofproto-dpif-mirror.h"
#include "ofproto-dpif-sflow.h"
#include "ofproto-dpif-upcall.h"
#include "ofproto-dpif-xlate.h"
#include "poll-loop.h"
#include "simap.h"
#include "smap.h"
#include "timer.h"
#include "tunnel.h"
#include "unaligned.h"
#include "unixctl.h"
#include "vlan-bitmap.h"
#include "vlog.h"

VLOG_DEFINE_THIS_MODULE(ofproto_dpif);

COVERAGE_DEFINE(ofproto_dpif_expired);
COVERAGE_DEFINE(facet_changed_rule);
COVERAGE_DEFINE(facet_revalidate);
COVERAGE_DEFINE(facet_unexpected);
COVERAGE_DEFINE(facet_suppress);
COVERAGE_DEFINE(subfacet_install_fail);
COVERAGE_DEFINE(packet_in_overflow);
COVERAGE_DEFINE(flow_mod_overflow);

/* Number of implemented OpenFlow tables. */
enum { N_TABLES = 255 };
enum { TBL_INTERNAL = N_TABLES - 1 };    /* Used for internal hidden rules. */
BUILD_ASSERT_DECL(N_TABLES >= 2 && N_TABLES <= 255);

struct flow_miss;
struct facet;

struct rule_dpif {
    struct rule up;

    /* These statistics:
     *
     *   - Do include packets and bytes from facets that have been deleted or
     *     whose own statistics have been folded into the rule.
     *
     *   - Do include packets and bytes sent "by hand" that were accounted to
     *     the rule without any facet being involved (this is a rare corner
     *     case in rule_execute()).
     *
     *   - Do not include packet or bytes that can be obtained from any facet's
     *     packet_count or byte_count member or that can be obtained from the
     *     datapath by, e.g., dpif_flow_get() for any subfacet.
     */
    struct ovs_mutex stats_mutex;
    uint64_t packet_count OVS_GUARDED;  /* Number of packets received. */
    uint64_t byte_count OVS_GUARDED;    /* Number of bytes received. */
};

static void rule_get_stats(struct rule *, uint64_t *packets, uint64_t *bytes);
static struct rule_dpif *rule_dpif_cast(const struct rule *);

struct ofbundle {
    struct hmap_node hmap_node; /* In struct ofproto's "bundles" hmap. */
    struct ofproto_dpif *ofproto; /* Owning ofproto. */
    void *aux;                  /* Key supplied by ofproto's client. */
    char *name;                 /* Identifier for log messages. */

    /* Configuration. */
    struct clist ports;          /* Contains "struct ofport"s. */
    enum port_vlan_mode vlan_mode; /* VLAN mode */
    int vlan;                   /* -1=trunk port, else a 12-bit VLAN ID. */
    unsigned long *trunks;      /* Bitmap of trunked VLANs, if 'vlan' == -1.
                                 * NULL if all VLANs are trunked. */
    struct lacp *lacp;          /* LACP if LACP is enabled, otherwise NULL. */
    struct bond *bond;          /* Nonnull iff more than one port. */
    bool use_priority_tags;     /* Use 802.1p tag for frames in VLAN 0? */

    /* Status. */
    bool floodable;          /* True if no port has OFPUTIL_PC_NO_FLOOD set. */
};

static void bundle_remove(struct ofport *);
static void bundle_update(struct ofbundle *);
static void bundle_destroy(struct ofbundle *);
static void bundle_del_port(struct ofport_dpif *);
static void bundle_run(struct ofbundle *);
static void bundle_wait(struct ofbundle *);

static void stp_run(struct ofproto_dpif *ofproto);
static void stp_wait(struct ofproto_dpif *ofproto);
static int set_stp_port(struct ofport *,
                        const struct ofproto_port_stp_settings *);

static void compose_slow_path(const struct ofproto_dpif *, const struct flow *,
                              enum slow_path_reason,
                              uint64_t *stub, size_t stub_size,
                              const struct nlattr **actionsp,
                              size_t *actions_lenp);

/* A subfacet (see "struct subfacet" below) has three possible installation
 * states:
 *
 *   - SF_NOT_INSTALLED: Not installed in the datapath.  This will only be the
 *     case just after the subfacet is created, just before the subfacet is
 *     destroyed, or if the datapath returns an error when we try to install a
 *     subfacet.
 *
 *   - SF_FAST_PATH: The subfacet's actions are installed in the datapath.
 *
 *   - SF_SLOW_PATH: An action that sends every packet for the subfacet through
 *     ofproto_dpif is installed in the datapath.
 */
enum subfacet_path {
    SF_NOT_INSTALLED,           /* No datapath flow for this subfacet. */
    SF_FAST_PATH,               /* Full actions are installed. */
    SF_SLOW_PATH,               /* Send-to-userspace action is installed. */
};

/* A dpif flow and actions associated with a facet.
 *
 * See also the large comment on struct facet. */
struct subfacet {
    /* Owners. */
    struct hmap_node hmap_node; /* In struct ofproto_dpif 'subfacets' list. */
    struct clist list_node;      /* In struct facet's 'facets' list. */
    struct facet *facet;        /* Owning facet. */
    struct dpif_backer *backer; /* Owning backer. */

    enum odp_key_fitness key_fitness;
    struct nlattr *key;
    int key_len;

    long long int used;         /* Time last used; time created if not used. */
    long long int created;      /* Time created. */

    uint64_t dp_packet_count;   /* Last known packet count in the datapath. */
    uint64_t dp_byte_count;     /* Last known byte count in the datapath. */

    enum subfacet_path path;    /* Installed in datapath? */
};

#define SUBFACET_DESTROY_MAX_BATCH 50

static struct subfacet *subfacet_create(struct facet *, struct flow_miss *);
static struct subfacet *subfacet_find(struct dpif_backer *,
                                      const struct nlattr *key, size_t key_len,
                                      uint32_t key_hash);
static void subfacet_destroy(struct subfacet *);
static void subfacet_destroy__(struct subfacet *);
static void subfacet_destroy_batch(struct dpif_backer *,
                                   struct subfacet **, int n);
static void subfacet_reset_dp_stats(struct subfacet *,
                                    struct dpif_flow_stats *);
static void subfacet_update_stats(struct subfacet *,
                                  const struct dpif_flow_stats *);
static int subfacet_install(struct subfacet *,
                            const struct ofpbuf *odp_actions,
                            struct dpif_flow_stats *);
static void subfacet_uninstall(struct subfacet *);

/* A unique, non-overlapping instantiation of an OpenFlow flow.
 *
 * A facet associates a "struct flow", which represents the Open vSwitch
 * userspace idea of an exact-match flow, with one or more subfacets.
 * While the facet is created based on an exact-match flow, it is stored
 * within the ofproto based on the wildcards that could be expressed
 * based on the flow table and other configuration.  (See the 'wc'
 * description in "struct xlate_out" for more details.)
 *
 * Each subfacet tracks the datapath's idea of the flow equivalent to
 * the facet.  When the kernel module (or other dpif implementation) and
 * Open vSwitch userspace agree on the definition of a flow key, there
 * is exactly one subfacet per facet.  If the dpif implementation
 * supports more-specific flow matching than userspace, however, a facet
 * can have more than one subfacet.  Examples include the dpif
 * implementation not supporting the same wildcards as userspace or some
 * distinction in flow that userspace simply doesn't understand.
 *
 * Flow expiration works in terms of subfacets, so a facet must have at
 * least one subfacet or it will never expire, leaking memory. */
struct facet {
    /* Owners. */
    struct hmap_node hmap_node;  /* In owning ofproto's 'facets' hmap. */
    struct ofproto_dpif *ofproto;

    /* Owned data. */
    struct clist subfacets;
    long long int used;         /* Time last used; time created if not used. */

    /* Key. */
    struct flow flow;           /* Flow of the creating subfacet. */
    struct cls_rule cr;         /* In 'ofproto_dpif's facets classifier. */

    /* These statistics:
     *
     *   - Do include packets and bytes sent "by hand", e.g. with
     *     dpif_execute().
     *
     *   - Do include packets and bytes that were obtained from the datapath
     *     when a subfacet's statistics were reset (e.g. dpif_flow_put() with
     *     DPIF_FP_ZERO_STATS).
     *
     *   - Do not include packets or bytes that can be obtained from the
     *     datapath for any existing subfacet.
     */
    uint64_t packet_count;       /* Number of packets received. */
    uint64_t byte_count;         /* Number of bytes received. */

    /* Resubmit statistics. */
    uint64_t prev_packet_count;  /* Number of packets from last stats push. */
    uint64_t prev_byte_count;    /* Number of bytes from last stats push. */
    long long int prev_used;     /* Used time from last stats push. */

    /* Accounting. */
    uint64_t accounted_bytes;    /* Bytes processed by facet_account(). */
    struct netflow_flow nf_flow; /* Per-flow NetFlow tracking data. */
    uint8_t tcp_flags;           /* TCP flags seen for this 'rule'. */

    struct xlate_out xout;

    /* Storage for a single subfacet, to reduce malloc() time and space
     * overhead.  (A facet always has at least one subfacet and in the common
     * case has exactly one subfacet.  However, 'one_subfacet' may not
     * always be valid, since it could have been removed after newer
     * subfacets were pushed onto the 'subfacets' list.) */
    struct subfacet one_subfacet;

    long long int learn_rl;      /* Rate limiter for facet_learn(). */
};

static struct facet *facet_create(const struct flow_miss *);
static void facet_remove(struct facet *);
static void facet_free(struct facet *);

static struct facet *facet_find(struct ofproto_dpif *, const struct flow *);
static struct facet *facet_lookup_valid(struct ofproto_dpif *,
                                        const struct flow *);
static bool facet_revalidate(struct facet *);
static bool facet_check_consistency(struct facet *);

static void facet_flush_stats(struct facet *);

static void facet_reset_counters(struct facet *);
static void flow_push_stats(struct ofproto_dpif *, struct flow *,
                            struct dpif_flow_stats *, bool may_learn);
static void facet_push_stats(struct facet *, bool may_learn);
static void facet_learn(struct facet *);
static void facet_account(struct facet *);
static void push_all_stats(void);

static bool facet_is_controller_flow(struct facet *);

struct ofport_dpif {
    struct hmap_node odp_port_node; /* In dpif_backer's "odp_to_ofport_map". */
    struct ofport up;

    odp_port_t odp_port;
    struct ofbundle *bundle;    /* Bundle that contains this port, if any. */
    struct clist bundle_node;    /* In struct ofbundle's "ports" list. */
    struct cfm *cfm;            /* Connectivity Fault Management, if any. */
    struct bfd *bfd;            /* BFD, if any. */
    bool may_enable;            /* May be enabled in bonds. */
    bool is_tunnel;             /* This port is a tunnel. */
    long long int carrier_seq;  /* Carrier status changes. */
    struct ofport_dpif *peer;   /* Peer if patch port. */

    /* Spanning tree. */
    struct stp_port *stp_port;  /* Spanning Tree Protocol, if any. */
    enum stp_state stp_state;   /* Always STP_DISABLED if STP not in use. */
    long long int stp_state_entered;

    /* Queue to DSCP mapping. */
    struct ofproto_port_queue *qdscp;
    size_t n_qdscp;

    /* Linux VLAN device support (e.g. "eth0.10" for VLAN 10.)
     *
     * This is deprecated.  It is only for compatibility with broken device
     * drivers in old versions of Linux that do not properly support VLANs when
     * VLAN devices are not used.  When broken device drivers are no longer in
     * widespread use, we will delete these interfaces. */
    ofp_port_t realdev_ofp_port;
    int vlandev_vid;
};

/* Linux VLAN device support (e.g. "eth0.10" for VLAN 10.)
 *
 * This is deprecated.  It is only for compatibility with broken device drivers
 * in old versions of Linux that do not properly support VLANs when VLAN
 * devices are not used.  When broken device drivers are no longer in
 * widespread use, we will delete these interfaces. */
struct vlan_splinter {
    struct hmap_node realdev_vid_node;
    struct hmap_node vlandev_node;
    ofp_port_t realdev_ofp_port;
    ofp_port_t vlandev_ofp_port;
    int vid;
};

static void vsp_remove(struct ofport_dpif *);
static void vsp_add(struct ofport_dpif *, ofp_port_t realdev_ofp_port, int vid);

static odp_port_t ofp_port_to_odp_port(const struct ofproto_dpif *,
                                       ofp_port_t);

static ofp_port_t odp_port_to_ofp_port(const struct ofproto_dpif *,
                                       odp_port_t);

static struct ofport_dpif *
ofport_dpif_cast(const struct ofport *ofport)
{
    return ofport ? CONTAINER_OF(ofport, struct ofport_dpif, up) : NULL;
}

static void port_run(struct ofport_dpif *);
static void port_run_fast(struct ofport_dpif *);
static void port_wait(struct ofport_dpif *);
static int set_bfd(struct ofport *, const struct smap *);
static int set_cfm(struct ofport *, const struct cfm_settings *);
static void ofport_update_peer(struct ofport_dpif *);
static void run_fast_rl(void);
static int run_fast(struct ofproto *);

struct dpif_completion {
    struct clist list_node;
    struct ofoperation *op;
};

/* Reasons that we might need to revalidate every facet, and corresponding
 * coverage counters.
 *
 * A value of 0 means that there is no need to revalidate.
 *
 * It would be nice to have some cleaner way to integrate with coverage
 * counters, but with only a few reasons I guess this is good enough for
 * now. */
enum revalidate_reason {
    REV_RECONFIGURE = 1,       /* Switch configuration changed. */
    REV_STP,                   /* Spanning tree protocol port status change. */
    REV_BOND,                  /* Bonding changed. */
    REV_PORT_TOGGLED,          /* Port enabled or disabled by CFM, LACP, ...*/
    REV_FLOW_TABLE,            /* Flow table changed. */
    REV_MAC_LEARNING,          /* Mac learning changed. */
    REV_INCONSISTENCY          /* Facet self-check failed. */
};
COVERAGE_DEFINE(rev_reconfigure);
COVERAGE_DEFINE(rev_stp);
COVERAGE_DEFINE(rev_bond);
COVERAGE_DEFINE(rev_port_toggled);
COVERAGE_DEFINE(rev_flow_table);
COVERAGE_DEFINE(rev_mac_learning);
COVERAGE_DEFINE(rev_inconsistency);

struct avg_subfacet_rates {
    double add_rate;   /* Moving average of new flows created per minute. */
    double del_rate;   /* Moving average of flows deleted per minute. */
};

/* All datapaths of a given type share a single dpif backer instance. */
struct dpif_backer {
    char *type;
    int refcount;
    struct dpif *dpif;
    struct udpif *udpif;
    struct timer next_expiration;

    struct ovs_rwlock odp_to_ofport_lock;
    struct hmap odp_to_ofport_map OVS_GUARDED; /* ODP port to ofport map. */

    struct simap tnl_backers;      /* Set of dpif ports backing tunnels. */

    /* Facet revalidation flags applying to facets which use this backer. */
    enum revalidate_reason need_revalidate; /* Revalidate every facet. */

    struct hmap drop_keys; /* Set of dropped odp keys. */
    bool recv_set_enable; /* Enables or disables receiving packets. */

    struct hmap subfacets;
    struct governor *governor;

    /* Subfacet statistics.
     *
     * These keep track of the total number of subfacets added and deleted and
     * flow life span.  They are useful for computing the flow rates stats
     * exposed via "ovs-appctl dpif/show".  The goal is to learn about
     * traffic patterns in ways that we can use later to improve Open vSwitch
     * performance in new situations.  */
    long long int created;           /* Time when it is created. */
    unsigned max_n_subfacet;         /* Maximum number of flows */
    unsigned avg_n_subfacet;         /* Average number of flows. */
    long long int avg_subfacet_life; /* Average life span of subfacets. */

    /* The average number of subfacets... */
    struct avg_subfacet_rates hourly;   /* ...over the last hour. */
    struct avg_subfacet_rates daily;    /* ...over the last day. */
    struct avg_subfacet_rates lifetime; /* ...over the switch lifetime. */
    long long int last_minute;          /* Last time 'hourly' was updated. */

    /* Number of subfacets added or deleted since 'last_minute'. */
    unsigned subfacet_add_count;
    unsigned subfacet_del_count;

    /* Number of subfacets added or deleted from 'created' to 'last_minute.' */
    unsigned long long int total_subfacet_add_count;
    unsigned long long int total_subfacet_del_count;

    /* Number of upcall handling threads. */
    unsigned int n_handler_threads;
};

/* All existing ofproto_backer instances, indexed by ofproto->up.type. */
static struct shash all_dpif_backers = SHASH_INITIALIZER(&all_dpif_backers);

static void drop_key_clear(struct dpif_backer *);
static void update_moving_averages(struct dpif_backer *backer);

struct ofproto_dpif {
    struct hmap_node all_ofproto_dpifs_node; /* In 'all_ofproto_dpifs'. */
    struct ofproto up;
    struct dpif_backer *backer;

    /* Special OpenFlow rules. */
    struct rule_dpif *miss_rule; /* Sends flow table misses to controller. */
    struct rule_dpif *no_packet_in_rule; /* Drops flow table misses. */
    struct rule_dpif *drop_frags_rule; /* Used in OFPC_FRAG_DROP mode. */

    /* Bridging. */
    struct netflow *netflow;
    struct dpif_sflow *sflow;
    struct dpif_ipfix *ipfix;
    struct hmap bundles;        /* Contains "struct ofbundle"s. */
    struct mac_learning *ml;
    bool has_bonded_bundles;
    struct mbridge *mbridge;

    /* Facets. */
    struct classifier facets;     /* Contains 'struct facet's. */
    long long int consistency_rl;

    struct netdev_stats stats; /* To account packets generated and consumed in
                                * userspace. */

    /* Spanning tree. */
    struct stp *stp;
    long long int stp_last_tick;

    /* VLAN splinters. */
    struct ovs_mutex vsp_mutex;
    struct hmap realdev_vid_map OVS_GUARDED; /* (realdev,vid) -> vlandev. */
    struct hmap vlandev_map OVS_GUARDED;     /* vlandev -> (realdev,vid). */

    /* Ports. */
    struct sset ports;             /* Set of standard port names. */
    struct sset ghost_ports;       /* Ports with no datapath port. */
    struct sset port_poll_set;     /* Queued names for port_poll() reply. */
    int port_poll_errno;           /* Last errno for port_poll() reply. */

    /* Per ofproto's dpif stats. */
    uint64_t n_hit;
    uint64_t n_missed;

    /* Work queues. */
    struct guarded_list pins;      /* Contains "struct ofputil_packet_in"s. */
};

/* By default, flows in the datapath are wildcarded (megaflows).  They
 * may be disabled with the "ovs-appctl dpif/disable-megaflows" command. */
static bool enable_megaflows = true;

/* All existing ofproto_dpif instances, indexed by ->up.name. */
static struct hmap all_ofproto_dpifs = HMAP_INITIALIZER(&all_ofproto_dpifs);

static void ofproto_dpif_unixctl_init(void);

static inline struct ofproto_dpif *
ofproto_dpif_cast(const struct ofproto *ofproto)
{
    ovs_assert(ofproto->ofproto_class == &ofproto_dpif_class);
    return CONTAINER_OF(ofproto, struct ofproto_dpif, up);
}

static struct ofport_dpif *get_ofp_port(const struct ofproto_dpif *ofproto,
                                        ofp_port_t ofp_port);
static void ofproto_trace(struct ofproto_dpif *, const struct flow *,
                          const struct ofpbuf *packet, struct ds *);

/* Upcalls. */
static void handle_upcalls(struct dpif_backer *);

/* Flow expiration. */
static int expire(struct dpif_backer *);

/* NetFlow. */
static void send_netflow_active_timeouts(struct ofproto_dpif *);

/* Utilities. */
static int send_packet(const struct ofport_dpif *, struct ofpbuf *packet);

/* Global variables. */
static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);

/* Initial mappings of port to bridge mappings. */
static struct shash init_ofp_ports = SHASH_INITIALIZER(&init_ofp_ports);

/* Executes 'fm'.  The caller retains ownership of 'fm' and everything in
 * it. */
void
ofproto_dpif_flow_mod(struct ofproto_dpif *ofproto,
                      struct ofputil_flow_mod *fm)
{
    ofproto_flow_mod(&ofproto->up, fm);
}

/* Appends 'pin' to the queue of "packet ins" to be sent to the controller.
 * Takes ownership of 'pin' and pin->packet. */
void
ofproto_dpif_send_packet_in(struct ofproto_dpif *ofproto,
                            struct ofputil_packet_in *pin)
{
    if (!guarded_list_push_back(&ofproto->pins, &pin->list_node, 1024)) {
        COVERAGE_INC(packet_in_overflow);
        free(CONST_CAST(void *, pin->packet));
        free(pin);
    }
}

/* Factory functions. */

static void
init(const struct shash *iface_hints)
{
    struct shash_node *node;

    /* Make a local copy, since we don't own 'iface_hints' elements. */
    SHASH_FOR_EACH(node, iface_hints) {
        const struct iface_hint *orig_hint = node->data;
        struct iface_hint *new_hint = xmalloc(sizeof *new_hint);

        new_hint->br_name = xstrdup(orig_hint->br_name);
        new_hint->br_type = xstrdup(orig_hint->br_type);
        new_hint->ofp_port = orig_hint->ofp_port;

        shash_add(&init_ofp_ports, node->name, new_hint);
    }
}

static void
enumerate_types(struct sset *types)
{
    dp_enumerate_types(types);
}

static int
enumerate_names(const char *type, struct sset *names)
{
    struct ofproto_dpif *ofproto;

    sset_clear(names);
    HMAP_FOR_EACH (ofproto, all_ofproto_dpifs_node, &all_ofproto_dpifs) {
        if (strcmp(type, ofproto->up.type)) {
            continue;
        }
        sset_add(names, ofproto->up.name);
    }

    return 0;
}

static int
del(const char *type, const char *name)
{
    struct dpif *dpif;
    int error;

    error = dpif_open(name, type, &dpif);
    if (!error) {
        error = dpif_delete(dpif);
        dpif_close(dpif);
    }
    return error;
}

static const char *
port_open_type(const char *datapath_type, const char *port_type)
{
    return dpif_port_open_type(datapath_type, port_type);
}

/* Type functions. */

static void process_dpif_port_changes(struct dpif_backer *);
static void process_dpif_all_ports_changed(struct dpif_backer *);
static void process_dpif_port_change(struct dpif_backer *,
                                     const char *devname);
static void process_dpif_port_error(struct dpif_backer *, int error);

static struct ofproto_dpif *
lookup_ofproto_dpif_by_port_name(const char *name)
{
    struct ofproto_dpif *ofproto;

    HMAP_FOR_EACH (ofproto, all_ofproto_dpifs_node, &all_ofproto_dpifs) {
        if (sset_contains(&ofproto->ports, name)) {
            return ofproto;
        }
    }

    return NULL;
}

static int
type_run(const char *type)
{
    static long long int push_timer = LLONG_MIN;
    struct dpif_backer *backer;

    backer = shash_find_data(&all_dpif_backers, type);
    if (!backer) {
        /* This is not necessarily a problem, since backers are only
         * created on demand. */
        return 0;
    }

    dpif_run(backer->dpif);

    /* The most natural place to push facet statistics is when they're pulled
     * from the datapath.  However, when there are many flows in the datapath,
     * this expensive operation can occur so frequently, that it reduces our
     * ability to quickly set up flows.  To reduce the cost, we push statistics
     * here instead. */
    if (time_msec() > push_timer) {
        push_timer = time_msec() + 2000;
        push_all_stats();
    }

    /* If vswitchd started with other_config:flow_restore_wait set as "true",
     * and the configuration has now changed to "false", enable receiving
     * packets from the datapath. */
    if (!backer->recv_set_enable && !ofproto_get_flow_restore_wait()) {
        int error;

        backer->recv_set_enable = true;

        error = dpif_recv_set(backer->dpif, backer->recv_set_enable);
        if (error) {
            udpif_recv_set(backer->udpif, 0, false);
            VLOG_ERR("Failed to enable receiving packets in dpif.");
            return error;
        }
        udpif_recv_set(backer->udpif, n_handler_threads,
                       backer->recv_set_enable);
        dpif_flow_flush(backer->dpif);
        backer->need_revalidate = REV_RECONFIGURE;
    }

    /* If the n_handler_threads is reconfigured, call udpif_recv_set()
     * to reset the handler threads. */
    if (backer->n_handler_threads != n_handler_threads) {
        udpif_recv_set(backer->udpif, n_handler_threads,
                       backer->recv_set_enable);
        backer->n_handler_threads = n_handler_threads;
    }

    if (backer->need_revalidate) {
        struct ofproto_dpif *ofproto;
        struct simap_node *node;
        struct simap tmp_backers;

        /* Handle tunnel garbage collection. */
        simap_init(&tmp_backers);
        simap_swap(&backer->tnl_backers, &tmp_backers);

        HMAP_FOR_EACH (ofproto, all_ofproto_dpifs_node, &all_ofproto_dpifs) {
            struct ofport_dpif *iter;

            if (backer != ofproto->backer) {
                continue;
            }

            HMAP_FOR_EACH (iter, up.hmap_node, &ofproto->up.ports) {
                char namebuf[NETDEV_VPORT_NAME_BUFSIZE];
                const char *dp_port;

                if (!iter->is_tunnel) {
                    continue;
                }

                dp_port = netdev_vport_get_dpif_port(iter->up.netdev,
                                                     namebuf, sizeof namebuf);
                node = simap_find(&tmp_backers, dp_port);
                if (node) {
                    simap_put(&backer->tnl_backers, dp_port, node->data);
                    simap_delete(&tmp_backers, node);
                    node = simap_find(&backer->tnl_backers, dp_port);
                } else {
                    node = simap_find(&backer->tnl_backers, dp_port);
                    if (!node) {
                        odp_port_t odp_port = ODPP_NONE;

                        if (!dpif_port_add(backer->dpif, iter->up.netdev,
                                           &odp_port)) {
                            simap_put(&backer->tnl_backers, dp_port,
                                      odp_to_u32(odp_port));
                            node = simap_find(&backer->tnl_backers, dp_port);
                        }
                    }
                }

                iter->odp_port = node ? u32_to_odp(node->data) : ODPP_NONE;
                if (tnl_port_reconfigure(iter, iter->up.netdev,
                                         iter->odp_port)) {
                    backer->need_revalidate = REV_RECONFIGURE;
                }
            }
        }

        SIMAP_FOR_EACH (node, &tmp_backers) {
            dpif_port_del(backer->dpif, u32_to_odp(node->data));
        }
        simap_destroy(&tmp_backers);

        switch (backer->need_revalidate) {
        case REV_RECONFIGURE:   COVERAGE_INC(rev_reconfigure);   break;
        case REV_STP:           COVERAGE_INC(rev_stp);           break;
        case REV_BOND:          COVERAGE_INC(rev_bond);          break;
        case REV_PORT_TOGGLED:  COVERAGE_INC(rev_port_toggled);  break;
        case REV_FLOW_TABLE:    COVERAGE_INC(rev_flow_table);    break;
        case REV_MAC_LEARNING:  COVERAGE_INC(rev_mac_learning);  break;
        case REV_INCONSISTENCY: COVERAGE_INC(rev_inconsistency); break;
        }
        backer->need_revalidate = 0;

        /* Clear the drop_keys in case we should now be accepting some
         * formerly dropped flows. */
        drop_key_clear(backer);

        HMAP_FOR_EACH (ofproto, all_ofproto_dpifs_node, &all_ofproto_dpifs) {
            struct facet *facet, *next;
            struct ofport_dpif *ofport;
            struct cls_cursor cursor;
            struct ofbundle *bundle;

            if (ofproto->backer != backer) {
                continue;
            }

            ovs_rwlock_wrlock(&xlate_rwlock);
            xlate_ofproto_set(ofproto, ofproto->up.name,
                              ofproto->backer->dpif, ofproto->miss_rule,
                              ofproto->no_packet_in_rule, ofproto->ml,
                              ofproto->stp, ofproto->mbridge,
                              ofproto->sflow, ofproto->ipfix,
                              ofproto->up.frag_handling,
                              ofproto->up.forward_bpdu,
                              connmgr_has_in_band(ofproto->up.connmgr),
                              ofproto->netflow != NULL);

            HMAP_FOR_EACH (bundle, hmap_node, &ofproto->bundles) {
                xlate_bundle_set(ofproto, bundle, bundle->name,
                                 bundle->vlan_mode, bundle->vlan,
                                 bundle->trunks, bundle->use_priority_tags,
                                 bundle->bond, bundle->lacp,
                                 bundle->floodable);
            }

            HMAP_FOR_EACH (ofport, up.hmap_node, &ofproto->up.ports) {
                int stp_port = ofport->stp_port
                    ? stp_port_no(ofport->stp_port)
                    : -1;
                xlate_ofport_set(ofproto, ofport->bundle, ofport,
                                 ofport->up.ofp_port, ofport->odp_port,
                                 ofport->up.netdev, ofport->cfm,
                                 ofport->bfd, ofport->peer, stp_port,
                                 ofport->qdscp, ofport->n_qdscp,
                                 ofport->up.pp.config, ofport->is_tunnel,
                                 ofport->may_enable);
            }
            ovs_rwlock_unlock(&xlate_rwlock);

            /* Only ofproto-dpif cares about the facet classifier so we just
             * lock cls_cursor_init() to appease the thread safety analysis. */
            ovs_rwlock_rdlock(&ofproto->facets.rwlock);
            cls_cursor_init(&cursor, &ofproto->facets, NULL);
            ovs_rwlock_unlock(&ofproto->facets.rwlock);
            CLS_CURSOR_FOR_EACH_SAFE (facet, next, cr, &cursor) {
                facet_revalidate(facet);
                run_fast_rl();
            }
        }

        udpif_revalidate(backer->udpif);
    }

    if (!backer->recv_set_enable) {
        /* Wake up before a max of 1000ms. */
        timer_set_duration(&backer->next_expiration, 1000);
    } else if (timer_expired(&backer->next_expiration)) {
        int delay = expire(backer);
        timer_set_duration(&backer->next_expiration, delay);
    }

    process_dpif_port_changes(backer);

    if (backer->governor) {
        size_t n_subfacets;

        governor_run(backer->governor);

        /* If the governor has shrunk to its minimum size and the number of
         * subfacets has dwindled, then drop the governor entirely.
         *
         * For hysteresis, the number of subfacets to drop the governor is
         * smaller than the number needed to trigger its creation. */
        n_subfacets = hmap_count(&backer->subfacets);
        if (n_subfacets * 4 < flow_eviction_threshold
            && governor_is_idle(backer->governor)) {
            governor_destroy(backer->governor);
            backer->governor = NULL;
        }
    }

    return 0;
}

/* Check for and handle port changes in 'backer''s dpif. */
static void
process_dpif_port_changes(struct dpif_backer *backer)
{
    for (;;) {
        char *devname;
        int error;

        error = dpif_port_poll(backer->dpif, &devname);
        switch (error) {
        case EAGAIN:
            return;

        case ENOBUFS:
            process_dpif_all_ports_changed(backer);
            break;

        case 0:
            process_dpif_port_change(backer, devname);
            free(devname);
            break;

        default:
            process_dpif_port_error(backer, error);
            break;
        }
    }
}

static void
process_dpif_all_ports_changed(struct dpif_backer *backer)
{
    struct ofproto_dpif *ofproto;
    struct dpif_port dpif_port;
    struct dpif_port_dump dump;
    struct sset devnames;
    const char *devname;

    sset_init(&devnames);
    HMAP_FOR_EACH (ofproto, all_ofproto_dpifs_node, &all_ofproto_dpifs) {
        if (ofproto->backer == backer) {
            struct ofport *ofport;

            HMAP_FOR_EACH (ofport, hmap_node, &ofproto->up.ports) {
                sset_add(&devnames, netdev_get_name(ofport->netdev));
            }
        }
    }
    DPIF_PORT_FOR_EACH (&dpif_port, &dump, backer->dpif) {
        sset_add(&devnames, dpif_port.name);
    }

    SSET_FOR_EACH (devname, &devnames) {
        process_dpif_port_change(backer, devname);
    }
    sset_destroy(&devnames);
}

static void
process_dpif_port_change(struct dpif_backer *backer, const char *devname)
{
    struct ofproto_dpif *ofproto;
    struct dpif_port port;

    /* Don't report on the datapath's device. */
    if (!strcmp(devname, dpif_base_name(backer->dpif))) {
        return;
    }

    HMAP_FOR_EACH (ofproto, all_ofproto_dpifs_node,
                   &all_ofproto_dpifs) {
        if (simap_contains(&ofproto->backer->tnl_backers, devname)) {
            return;
        }
    }

    ofproto = lookup_ofproto_dpif_by_port_name(devname);
    if (dpif_port_query_by_name(backer->dpif, devname, &port)) {
        /* The port was removed.  If we know the datapath,
         * report it through poll_set().  If we don't, it may be
         * notifying us of a removal we initiated, so ignore it.
         * If there's a pending ENOBUFS, let it stand, since
         * everything will be reevaluated. */
        if (ofproto && ofproto->port_poll_errno != ENOBUFS) {
            sset_add(&ofproto->port_poll_set, devname);
            ofproto->port_poll_errno = 0;
        }
    } else if (!ofproto) {
        /* The port was added, but we don't know with which
         * ofproto we should associate it.  Delete it. */
        dpif_port_del(backer->dpif, port.port_no);
    } else {
        struct ofport_dpif *ofport;

        ofport = ofport_dpif_cast(shash_find_data(
                                      &ofproto->up.port_by_name, devname));
        if (ofport
            && ofport->odp_port != port.port_no
            && !odp_port_to_ofport(backer, port.port_no))
        {
            /* 'ofport''s datapath port number has changed from
             * 'ofport->odp_port' to 'port.port_no'.  Update our internal data
             * structures to match. */
            ovs_rwlock_wrlock(&backer->odp_to_ofport_lock);
            hmap_remove(&backer->odp_to_ofport_map, &ofport->odp_port_node);
            ofport->odp_port = port.port_no;
            hmap_insert(&backer->odp_to_ofport_map, &ofport->odp_port_node,
                        hash_odp_port(port.port_no));
            ovs_rwlock_unlock(&backer->odp_to_ofport_lock);
            backer->need_revalidate = REV_RECONFIGURE;
        }
    }
    dpif_port_destroy(&port);
}

/* Propagate 'error' to all ofprotos based on 'backer'. */
static void
process_dpif_port_error(struct dpif_backer *backer, int error)
{
    struct ofproto_dpif *ofproto;

    HMAP_FOR_EACH (ofproto, all_ofproto_dpifs_node, &all_ofproto_dpifs) {
        if (ofproto->backer == backer) {
            sset_clear(&ofproto->port_poll_set);
            ofproto->port_poll_errno = error;
        }
    }
}

static int
dpif_backer_run_fast(struct dpif_backer *backer)
{
    handle_upcalls(backer);

    return 0;
}

static int
type_run_fast(const char *type)
{
    struct dpif_backer *backer;

    backer = shash_find_data(&all_dpif_backers, type);
    if (!backer) {
        /* This is not necessarily a problem, since backers are only
         * created on demand. */
        return 0;
    }

    return dpif_backer_run_fast(backer);
}

static void
run_fast_rl(void)
{
    static long long int port_rl = LLONG_MIN;

    if (time_msec() >= port_rl) {
        struct ofproto_dpif *ofproto;

        HMAP_FOR_EACH (ofproto, all_ofproto_dpifs_node, &all_ofproto_dpifs) {
            run_fast(&ofproto->up);
        }
        port_rl = time_msec() + 200;
    }
}

static void
type_wait(const char *type)
{
    struct dpif_backer *backer;

    backer = shash_find_data(&all_dpif_backers, type);
    if (!backer) {
        /* This is not necessarily a problem, since backers are only
         * created on demand. */
        return;
    }

    if (backer->governor) {
        governor_wait(backer->governor);
    }

    timer_wait(&backer->next_expiration);
    dpif_wait(backer->dpif);
    udpif_wait(backer->udpif);
}

/* Basic life-cycle. */

static int add_internal_flows(struct ofproto_dpif *);

static struct ofproto *
alloc(void)
{
    struct ofproto_dpif *ofproto = xmalloc(sizeof *ofproto);
    return &ofproto->up;
}

static void
dealloc(struct ofproto *ofproto_)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    free(ofproto);
}

static void
close_dpif_backer(struct dpif_backer *backer)
{
    struct shash_node *node;

    ovs_assert(backer->refcount > 0);

    if (--backer->refcount) {
        return;
    }

    drop_key_clear(backer);
    hmap_destroy(&backer->drop_keys);

    udpif_destroy(backer->udpif);

    simap_destroy(&backer->tnl_backers);
    ovs_rwlock_destroy(&backer->odp_to_ofport_lock);
    hmap_destroy(&backer->odp_to_ofport_map);
    node = shash_find(&all_dpif_backers, backer->type);
    free(backer->type);
    shash_delete(&all_dpif_backers, node);
    dpif_close(backer->dpif);

    ovs_assert(hmap_is_empty(&backer->subfacets));
    hmap_destroy(&backer->subfacets);
    governor_destroy(backer->governor);

    free(backer);
}

/* Datapath port slated for removal from datapath. */
struct odp_garbage {
    struct clist list_node;
    odp_port_t odp_port;
};

static int
open_dpif_backer(const char *type, struct dpif_backer **backerp)
{
    struct dpif_backer *backer;
    struct dpif_port_dump port_dump;
    struct dpif_port port;
    struct shash_node *node;
    struct clist garbage_list;
    struct odp_garbage *garbage, *next;
    struct sset names;
    char *backer_name;
    const char *name;
    int error;

    backer = shash_find_data(&all_dpif_backers, type);
    if (backer) {
        backer->refcount++;
        *backerp = backer;
        return 0;
    }

    backer_name = xasprintf("ovs-%s", type);

    /* Remove any existing datapaths, since we assume we're the only
     * userspace controlling the datapath. */
    sset_init(&names);
    dp_enumerate_names(type, &names);
    SSET_FOR_EACH(name, &names) {
        struct dpif *old_dpif;

        /* Don't remove our backer if it exists. */
        if (!strcmp(name, backer_name)) {
            continue;
        }

        if (dpif_open(name, type, &old_dpif)) {
            VLOG_WARN("couldn't open old datapath %s to remove it", name);
        } else {
            dpif_delete(old_dpif);
            dpif_close(old_dpif);
        }
    }
    sset_destroy(&names);

    backer = xmalloc(sizeof *backer);

    error = dpif_create_and_open(backer_name, type, &backer->dpif);
    free(backer_name);
    if (error) {
        VLOG_ERR("failed to open datapath of type %s: %s", type,
                 ovs_strerror(error));
        free(backer);
        return error;
    }
    backer->udpif = udpif_create(backer, backer->dpif);

    backer->type = xstrdup(type);
    backer->governor = NULL;
    backer->refcount = 1;
    hmap_init(&backer->odp_to_ofport_map);
    ovs_rwlock_init(&backer->odp_to_ofport_lock);
    hmap_init(&backer->drop_keys);
    hmap_init(&backer->subfacets);
    timer_set_duration(&backer->next_expiration, 1000);
    backer->need_revalidate = 0;
    simap_init(&backer->tnl_backers);
    backer->recv_set_enable = !ofproto_get_flow_restore_wait();
    *backerp = backer;

    if (backer->recv_set_enable) {
        dpif_flow_flush(backer->dpif);
    }

    /* Loop through the ports already on the datapath and remove any
     * that we don't need anymore. */
    list_init(&garbage_list);
    dpif_port_dump_start(&port_dump, backer->dpif);
    while (dpif_port_dump_next(&port_dump, &port)) {
        node = shash_find(&init_ofp_ports, port.name);
        if (!node && strcmp(port.name, dpif_base_name(backer->dpif))) {
            garbage = xmalloc(sizeof *garbage);
            garbage->odp_port = port.port_no;
            list_push_front(&garbage_list, &garbage->list_node);
        }
    }
    dpif_port_dump_done(&port_dump);

    LIST_FOR_EACH_SAFE (garbage, next, list_node, &garbage_list) {
        dpif_port_del(backer->dpif, garbage->odp_port);
        list_remove(&garbage->list_node);
        free(garbage);
    }

    shash_add(&all_dpif_backers, type, backer);

    error = dpif_recv_set(backer->dpif, backer->recv_set_enable);
    if (error) {
        VLOG_ERR("failed to listen on datapath of type %s: %s",
                 type, ovs_strerror(error));
        close_dpif_backer(backer);
        return error;
    }
    udpif_recv_set(backer->udpif, n_handler_threads,
                   backer->recv_set_enable);
    backer->n_handler_threads = n_handler_threads;

    backer->max_n_subfacet = 0;
    backer->created = time_msec();
    backer->last_minute = backer->created;
    memset(&backer->hourly, 0, sizeof backer->hourly);
    memset(&backer->daily, 0, sizeof backer->daily);
    memset(&backer->lifetime, 0, sizeof backer->lifetime);
    backer->subfacet_add_count = 0;
    backer->subfacet_del_count = 0;
    backer->total_subfacet_add_count = 0;
    backer->total_subfacet_del_count = 0;
    backer->avg_n_subfacet = 0;
    backer->avg_subfacet_life = 0;

    return error;
}

static int
construct(struct ofproto *ofproto_)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    struct shash_node *node, *next;
    uint32_t max_ports;
    int error;

    error = open_dpif_backer(ofproto->up.type, &ofproto->backer);
    if (error) {
        return error;
    }

    max_ports = dpif_get_max_ports(ofproto->backer->dpif);
    ofproto_init_max_ports(ofproto_, MIN(max_ports, ofp_to_u16(OFPP_MAX)));

    ofproto->netflow = NULL;
    ofproto->sflow = NULL;
    ofproto->ipfix = NULL;
    ofproto->stp = NULL;
    hmap_init(&ofproto->bundles);
    ofproto->ml = mac_learning_create(MAC_ENTRY_DEFAULT_IDLE_TIME);
    ofproto->mbridge = mbridge_create();
    ofproto->has_bonded_bundles = false;
    ovs_mutex_init(&ofproto->vsp_mutex);

    classifier_init(&ofproto->facets);
    ofproto->consistency_rl = LLONG_MIN;

    guarded_list_init(&ofproto->pins);

    ofproto_dpif_unixctl_init();

    hmap_init(&ofproto->vlandev_map);
    hmap_init(&ofproto->realdev_vid_map);

    sset_init(&ofproto->ports);
    sset_init(&ofproto->ghost_ports);
    sset_init(&ofproto->port_poll_set);
    ofproto->port_poll_errno = 0;

    SHASH_FOR_EACH_SAFE (node, next, &init_ofp_ports) {
        struct iface_hint *iface_hint = node->data;

        if (!strcmp(iface_hint->br_name, ofproto->up.name)) {
            /* Check if the datapath already has this port. */
            if (dpif_port_exists(ofproto->backer->dpif, node->name)) {
                sset_add(&ofproto->ports, node->name);
            }

            free(iface_hint->br_name);
            free(iface_hint->br_type);
            free(iface_hint);
            shash_delete(&init_ofp_ports, node);
        }
    }

    hmap_insert(&all_ofproto_dpifs, &ofproto->all_ofproto_dpifs_node,
                hash_string(ofproto->up.name, 0));
    memset(&ofproto->stats, 0, sizeof ofproto->stats);

    ofproto_init_tables(ofproto_, N_TABLES);
    error = add_internal_flows(ofproto);
    ofproto->up.tables[TBL_INTERNAL].flags = OFTABLE_HIDDEN | OFTABLE_READONLY;

    ofproto->n_hit = 0;
    ofproto->n_missed = 0;

    return error;
}

static int
add_internal_flow(struct ofproto_dpif *ofproto, int id,
                  const struct ofpbuf *ofpacts, struct rule_dpif **rulep)
{
    struct ofputil_flow_mod fm;
    int error;

    match_init_catchall(&fm.match);
    fm.priority = 0;
    match_set_reg(&fm.match, 0, id);
    fm.new_cookie = htonll(0);
    fm.cookie = htonll(0);
    fm.cookie_mask = htonll(0);
    fm.modify_cookie = false;
    fm.table_id = TBL_INTERNAL;
    fm.command = OFPFC_ADD;
    fm.idle_timeout = 0;
    fm.hard_timeout = 0;
    fm.buffer_id = 0;
    fm.out_port = 0;
    fm.flags = 0;
    fm.ofpacts = ofpacts->data;
    fm.ofpacts_len = ofpacts->size;

    error = ofproto_flow_mod(&ofproto->up, &fm);
    if (error) {
        VLOG_ERR_RL(&rl, "failed to add internal flow %d (%s)",
                    id, ofperr_to_string(error));
        return error;
    }

    if (rule_dpif_lookup_in_table(ofproto, &fm.match.flow, NULL, TBL_INTERNAL,
                                  rulep)) {
        rule_dpif_unref(*rulep);
    } else {
        NOT_REACHED();
    }

    return 0;
}

static int
add_internal_flows(struct ofproto_dpif *ofproto)
{
    struct ofpact_controller *controller;
    uint64_t ofpacts_stub[128 / 8];
    struct ofpbuf ofpacts;
    int error;
    int id;

    ofpbuf_use_stack(&ofpacts, ofpacts_stub, sizeof ofpacts_stub);
    id = 1;

    controller = ofpact_put_CONTROLLER(&ofpacts);
    controller->max_len = UINT16_MAX;
    controller->controller_id = 0;
    controller->reason = OFPR_NO_MATCH;
    ofpact_pad(&ofpacts);

    error = add_internal_flow(ofproto, id++, &ofpacts, &ofproto->miss_rule);
    if (error) {
        return error;
    }

    ofpbuf_clear(&ofpacts);
    error = add_internal_flow(ofproto, id++, &ofpacts,
                              &ofproto->no_packet_in_rule);
    if (error) {
        return error;
    }

    error = add_internal_flow(ofproto, id++, &ofpacts,
                              &ofproto->drop_frags_rule);
    return error;
}

static void
destruct(struct ofproto *ofproto_)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    struct rule_dpif *rule, *next_rule;
    struct ofputil_packet_in *pin, *next_pin;
    struct facet *facet, *next_facet;
    struct cls_cursor cursor;
    struct oftable *table;
    struct clist pins;

    ovs_rwlock_rdlock(&ofproto->facets.rwlock);
    cls_cursor_init(&cursor, &ofproto->facets, NULL);
    ovs_rwlock_unlock(&ofproto->facets.rwlock);
    CLS_CURSOR_FOR_EACH_SAFE (facet, next_facet, cr, &cursor) {
        facet_remove(facet);
    }

    ofproto->backer->need_revalidate = REV_RECONFIGURE;
    ovs_rwlock_wrlock(&xlate_rwlock);
    xlate_remove_ofproto(ofproto);
    ovs_rwlock_unlock(&xlate_rwlock);

    /* Discard any flow_miss_batches queued up for 'ofproto', avoiding a
     * use-after-free error. */
    udpif_revalidate(ofproto->backer->udpif);

    hmap_remove(&all_ofproto_dpifs, &ofproto->all_ofproto_dpifs_node);

    OFPROTO_FOR_EACH_TABLE (table, &ofproto->up) {
        struct cls_cursor cursor;

        ovs_rwlock_rdlock(&table->cls.rwlock);
        cls_cursor_init(&cursor, &table->cls, NULL);
        ovs_rwlock_unlock(&table->cls.rwlock);
        CLS_CURSOR_FOR_EACH_SAFE (rule, next_rule, up.cr, &cursor) {
            ofproto_rule_delete(&ofproto->up, &rule->up);
        }
    }

    guarded_list_pop_all(&ofproto->pins, &pins);
    LIST_FOR_EACH_SAFE (pin, next_pin, list_node, &pins) {
        list_remove(&pin->list_node);
        free(CONST_CAST(void *, pin->packet));
        free(pin);
    }
    guarded_list_destroy(&ofproto->pins);

    mbridge_unref(ofproto->mbridge);

    netflow_destroy(ofproto->netflow);
    dpif_sflow_unref(ofproto->sflow);
    hmap_destroy(&ofproto->bundles);
    mac_learning_unref(ofproto->ml);

    classifier_destroy(&ofproto->facets);

    hmap_destroy(&ofproto->vlandev_map);
    hmap_destroy(&ofproto->realdev_vid_map);

    sset_destroy(&ofproto->ports);
    sset_destroy(&ofproto->ghost_ports);
    sset_destroy(&ofproto->port_poll_set);

    ovs_mutex_destroy(&ofproto->vsp_mutex);

    close_dpif_backer(ofproto->backer);
}

static int
run_fast(struct ofproto *ofproto_)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    struct ofputil_packet_in *pin, *next_pin;
    struct ofport_dpif *ofport;
    struct clist pins;

    /* Do not perform any periodic activity required by 'ofproto' while
     * waiting for flow restore to complete. */
    if (ofproto_get_flow_restore_wait()) {
        return 0;
    }

    guarded_list_pop_all(&ofproto->pins, &pins);
    LIST_FOR_EACH_SAFE (pin, next_pin, list_node, &pins) {
        connmgr_send_packet_in(ofproto->up.connmgr, pin);
        list_remove(&pin->list_node);
        free(CONST_CAST(void *, pin->packet));
        free(pin);
    }

    HMAP_FOR_EACH (ofport, up.hmap_node, &ofproto->up.ports) {
        port_run_fast(ofport);
    }

    return 0;
}

static int
run(struct ofproto *ofproto_)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    struct ofport_dpif *ofport;
    struct ofbundle *bundle;
    int error;

    if (mbridge_need_revalidate(ofproto->mbridge)) {
        ofproto->backer->need_revalidate = REV_RECONFIGURE;
        ovs_rwlock_wrlock(&ofproto->ml->rwlock);
        mac_learning_flush(ofproto->ml);
        ovs_rwlock_unlock(&ofproto->ml->rwlock);
    }

    /* Do not perform any periodic activity below required by 'ofproto' while
     * waiting for flow restore to complete. */
    if (ofproto_get_flow_restore_wait()) {
        return 0;
    }

    error = run_fast(ofproto_);
    if (error) {
        return error;
    }

    if (ofproto->netflow) {
        if (netflow_run(ofproto->netflow)) {
            send_netflow_active_timeouts(ofproto);
        }
    }
    if (ofproto->sflow) {
        dpif_sflow_run(ofproto->sflow);
    }
    if (ofproto->ipfix) {
        dpif_ipfix_run(ofproto->ipfix);
    }

    HMAP_FOR_EACH (ofport, up.hmap_node, &ofproto->up.ports) {
        port_run(ofport);
    }
    HMAP_FOR_EACH (bundle, hmap_node, &ofproto->bundles) {
        bundle_run(bundle);
    }

    stp_run(ofproto);
    ovs_rwlock_wrlock(&ofproto->ml->rwlock);
    if (mac_learning_run(ofproto->ml)) {
        ofproto->backer->need_revalidate = REV_MAC_LEARNING;
    }
    ovs_rwlock_unlock(&ofproto->ml->rwlock);

    /* Check the consistency of a random facet, to aid debugging. */
    ovs_rwlock_rdlock(&ofproto->facets.rwlock);
    if (time_msec() >= ofproto->consistency_rl
        && !classifier_is_empty(&ofproto->facets)
        && !ofproto->backer->need_revalidate) {
        struct cls_table *table;
        struct cls_rule *cr;
        struct facet *facet;

        ofproto->consistency_rl = time_msec() + 250;

        table = CONTAINER_OF(hmap_random_node(&ofproto->facets.tables),
                             struct cls_table, hmap_node);
        cr = CONTAINER_OF(hmap_random_node(&table->rules), struct cls_rule,
                          hmap_node);
        facet = CONTAINER_OF(cr, struct facet, cr);

        if (!facet_check_consistency(facet)) {
            ofproto->backer->need_revalidate = REV_INCONSISTENCY;
        }
    }
    ovs_rwlock_unlock(&ofproto->facets.rwlock);

    return 0;
}

static void
wait(struct ofproto *ofproto_)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    struct ofport_dpif *ofport;
    struct ofbundle *bundle;

    if (ofproto_get_flow_restore_wait()) {
        return;
    }

    if (ofproto->sflow) {
        dpif_sflow_wait(ofproto->sflow);
    }
    if (ofproto->ipfix) {
        dpif_ipfix_wait(ofproto->ipfix);
    }
    HMAP_FOR_EACH (ofport, up.hmap_node, &ofproto->up.ports) {
        port_wait(ofport);
    }
    HMAP_FOR_EACH (bundle, hmap_node, &ofproto->bundles) {
        bundle_wait(bundle);
    }
    if (ofproto->netflow) {
        netflow_wait(ofproto->netflow);
    }
    ovs_rwlock_rdlock(&ofproto->ml->rwlock);
    mac_learning_wait(ofproto->ml);
    ovs_rwlock_unlock(&ofproto->ml->rwlock);
    stp_wait(ofproto);
    if (ofproto->backer->need_revalidate) {
        /* Shouldn't happen, but if it does just go around again. */
        VLOG_DBG_RL(&rl, "need revalidate in ofproto_wait_cb()");
        poll_immediate_wake();
    }
}

static void
get_memory_usage(const struct ofproto *ofproto_, struct simap *usage)
{
    const struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    struct cls_cursor cursor;
    size_t n_subfacets = 0;
    struct facet *facet;

    ovs_rwlock_rdlock(&ofproto->facets.rwlock);
    simap_increase(usage, "facets", classifier_count(&ofproto->facets));
    ovs_rwlock_unlock(&ofproto->facets.rwlock);

    ovs_rwlock_rdlock(&ofproto->facets.rwlock);
    cls_cursor_init(&cursor, &ofproto->facets, NULL);
    CLS_CURSOR_FOR_EACH (facet, cr, &cursor) {
        n_subfacets += list_size(&facet->subfacets);
    }
    ovs_rwlock_unlock(&ofproto->facets.rwlock);
    simap_increase(usage, "subfacets", n_subfacets);
}

static void
flush(struct ofproto *ofproto_)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    struct subfacet *subfacet, *next_subfacet;
    struct subfacet *batch[SUBFACET_DESTROY_MAX_BATCH];
    int n_batch;

    n_batch = 0;
    HMAP_FOR_EACH_SAFE (subfacet, next_subfacet, hmap_node,
                        &ofproto->backer->subfacets) {
        if (subfacet->facet->ofproto != ofproto) {
            continue;
        }

        if (subfacet->path != SF_NOT_INSTALLED) {
            batch[n_batch++] = subfacet;
            if (n_batch >= SUBFACET_DESTROY_MAX_BATCH) {
                subfacet_destroy_batch(ofproto->backer, batch, n_batch);
                n_batch = 0;
            }
        } else {
            subfacet_destroy(subfacet);
        }
    }

    if (n_batch > 0) {
        subfacet_destroy_batch(ofproto->backer, batch, n_batch);
    }
}

static void
get_features(struct ofproto *ofproto_ OVS_UNUSED,
             bool *arp_match_ip, enum ofputil_action_bitmap *actions)
{
    *arp_match_ip = true;
    *actions = (OFPUTIL_A_OUTPUT |
                OFPUTIL_A_SET_VLAN_VID |
                OFPUTIL_A_SET_VLAN_PCP |
                OFPUTIL_A_STRIP_VLAN |
                OFPUTIL_A_SET_DL_SRC |
                OFPUTIL_A_SET_DL_DST |
                OFPUTIL_A_SET_NW_SRC |
                OFPUTIL_A_SET_NW_DST |
                OFPUTIL_A_SET_NW_TOS |
                OFPUTIL_A_SET_TP_SRC |
                OFPUTIL_A_SET_TP_DST |
                OFPUTIL_A_ENQUEUE);
}

static void
get_tables(struct ofproto *ofproto_, struct ofp12_table_stats *ots)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    struct dpif_dp_stats s;
    uint64_t n_miss, n_no_pkt_in, n_bytes, n_dropped_frags;
    uint64_t n_lookup;

    strcpy(ots->name, "classifier");

    dpif_get_dp_stats(ofproto->backer->dpif, &s);
    rule_get_stats(&ofproto->miss_rule->up, &n_miss, &n_bytes);
    rule_get_stats(&ofproto->no_packet_in_rule->up, &n_no_pkt_in, &n_bytes);
    rule_get_stats(&ofproto->drop_frags_rule->up, &n_dropped_frags, &n_bytes);

    n_lookup = s.n_hit + s.n_missed - n_dropped_frags;
    ots->lookup_count = htonll(n_lookup);
    ots->matched_count = htonll(n_lookup - n_miss - n_no_pkt_in);
}

static struct ofport *
port_alloc(void)
{
    struct ofport_dpif *port = xmalloc(sizeof *port);
    return &port->up;
}

static void
port_dealloc(struct ofport *port_)
{
    struct ofport_dpif *port = ofport_dpif_cast(port_);
    free(port);
}

static int
port_construct(struct ofport *port_)
{
    struct ofport_dpif *port = ofport_dpif_cast(port_);
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(port->up.ofproto);
    const struct netdev *netdev = port->up.netdev;
    char namebuf[NETDEV_VPORT_NAME_BUFSIZE];
    struct dpif_port dpif_port;
    int error;

    ofproto->backer->need_revalidate = REV_RECONFIGURE;
    port->bundle = NULL;
    port->cfm = NULL;
    port->bfd = NULL;
    port->may_enable = true;
    port->stp_port = NULL;
    port->stp_state = STP_DISABLED;
    port->is_tunnel = false;
    port->peer = NULL;
    port->qdscp = NULL;
    port->n_qdscp = 0;
    port->realdev_ofp_port = 0;
    port->vlandev_vid = 0;
    port->carrier_seq = netdev_get_carrier_resets(netdev);

    if (netdev_vport_is_patch(netdev)) {
        /* By bailing out here, we don't submit the port to the sFlow module
	 * to be considered for counter polling export.  This is correct
	 * because the patch port represents an interface that sFlow considers
	 * to be "internal" to the switch as a whole, and therefore not an
	 * candidate for counter polling. */
        port->odp_port = ODPP_NONE;
        ofport_update_peer(port);
        return 0;
    }

    error = dpif_port_query_by_name(ofproto->backer->dpif,
                                    netdev_vport_get_dpif_port(netdev, namebuf,
                                                               sizeof namebuf),
                                    &dpif_port);
    if (error) {
        return error;
    }

    port->odp_port = dpif_port.port_no;

    if (netdev_get_tunnel_config(netdev)) {
        tnl_port_add(port, port->up.netdev, port->odp_port);
        port->is_tunnel = true;
    } else {
        /* Sanity-check that a mapping doesn't already exist.  This
         * shouldn't happen for non-tunnel ports. */
        if (odp_port_to_ofp_port(ofproto, port->odp_port) != OFPP_NONE) {
            VLOG_ERR("port %s already has an OpenFlow port number",
                     dpif_port.name);
            dpif_port_destroy(&dpif_port);
            return EBUSY;
        }

        ovs_rwlock_wrlock(&ofproto->backer->odp_to_ofport_lock);
        hmap_insert(&ofproto->backer->odp_to_ofport_map, &port->odp_port_node,
                    hash_odp_port(port->odp_port));
        ovs_rwlock_unlock(&ofproto->backer->odp_to_ofport_lock);
    }
    dpif_port_destroy(&dpif_port);

    if (ofproto->sflow) {
        dpif_sflow_add_port(ofproto->sflow, port_, port->odp_port);
    }

    return 0;
}

static void
port_destruct(struct ofport *port_)
{
    struct ofport_dpif *port = ofport_dpif_cast(port_);
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(port->up.ofproto);
    const char *devname = netdev_get_name(port->up.netdev);
    char namebuf[NETDEV_VPORT_NAME_BUFSIZE];
    const char *dp_port_name;

    ofproto->backer->need_revalidate = REV_RECONFIGURE;
    ovs_rwlock_wrlock(&xlate_rwlock);
    xlate_ofport_remove(port);
    ovs_rwlock_unlock(&xlate_rwlock);

    dp_port_name = netdev_vport_get_dpif_port(port->up.netdev, namebuf,
                                              sizeof namebuf);
    if (dpif_port_exists(ofproto->backer->dpif, dp_port_name)) {
        /* The underlying device is still there, so delete it.  This
         * happens when the ofproto is being destroyed, since the caller
         * assumes that removal of attached ports will happen as part of
         * destruction. */
        if (!port->is_tunnel) {
            dpif_port_del(ofproto->backer->dpif, port->odp_port);
        }
    }

    if (port->peer) {
        port->peer->peer = NULL;
        port->peer = NULL;
    }

    if (port->odp_port != ODPP_NONE && !port->is_tunnel) {
        ovs_rwlock_wrlock(&ofproto->backer->odp_to_ofport_lock);
        hmap_remove(&ofproto->backer->odp_to_ofport_map, &port->odp_port_node);
        ovs_rwlock_unlock(&ofproto->backer->odp_to_ofport_lock);
    }

    tnl_port_del(port);
    sset_find_and_delete(&ofproto->ports, devname);
    sset_find_and_delete(&ofproto->ghost_ports, devname);
    bundle_remove(port_);
    set_cfm(port_, NULL);
    set_bfd(port_, NULL);
    if (ofproto->sflow) {
        dpif_sflow_del_port(ofproto->sflow, port->odp_port);
    }

    free(port->qdscp);
}

static void
port_modified(struct ofport *port_)
{
    struct ofport_dpif *port = ofport_dpif_cast(port_);

    if (port->bundle && port->bundle->bond) {
        bond_slave_set_netdev(port->bundle->bond, port, port->up.netdev);
    }

    if (port->cfm) {
        cfm_set_netdev(port->cfm, port->up.netdev);
    }

    if (port->bfd) {
        bfd_set_netdev(port->bfd, port->up.netdev);
    }

    if (port->is_tunnel && tnl_port_reconfigure(port, port->up.netdev,
                                                port->odp_port)) {
        ofproto_dpif_cast(port->up.ofproto)->backer->need_revalidate =
            REV_RECONFIGURE;
    }

    ofport_update_peer(port);
}

static void
port_reconfigured(struct ofport *port_, enum ofputil_port_config old_config)
{
    struct ofport_dpif *port = ofport_dpif_cast(port_);
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(port->up.ofproto);
    enum ofputil_port_config changed = old_config ^ port->up.pp.config;

    if (changed & (OFPUTIL_PC_NO_RECV | OFPUTIL_PC_NO_RECV_STP |
                   OFPUTIL_PC_NO_FWD | OFPUTIL_PC_NO_FLOOD |
                   OFPUTIL_PC_NO_PACKET_IN)) {
        ofproto->backer->need_revalidate = REV_RECONFIGURE;

        if (changed & OFPUTIL_PC_NO_FLOOD && port->bundle) {
            bundle_update(port->bundle);
        }
    }
}

static int
set_sflow(struct ofproto *ofproto_,
          const struct ofproto_sflow_options *sflow_options)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    struct dpif_sflow *ds = ofproto->sflow;

    if (sflow_options) {
        if (!ds) {
            struct ofport_dpif *ofport;

            ds = ofproto->sflow = dpif_sflow_create();
            HMAP_FOR_EACH (ofport, up.hmap_node, &ofproto->up.ports) {
                dpif_sflow_add_port(ds, &ofport->up, ofport->odp_port);
            }
            ofproto->backer->need_revalidate = REV_RECONFIGURE;
        }
        dpif_sflow_set_options(ds, sflow_options);
    } else {
        if (ds) {
            dpif_sflow_unref(ds);
            ofproto->backer->need_revalidate = REV_RECONFIGURE;
            ofproto->sflow = NULL;
        }
    }
    return 0;
}

static int
set_ipfix(
    struct ofproto *ofproto_,
    const struct ofproto_ipfix_bridge_exporter_options *bridge_exporter_options,
    const struct ofproto_ipfix_flow_exporter_options *flow_exporters_options,
    size_t n_flow_exporters_options)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    struct dpif_ipfix *di = ofproto->ipfix;
    bool has_options = bridge_exporter_options || flow_exporters_options;

    if (has_options && !di) {
        di = ofproto->ipfix = dpif_ipfix_create();
    }

    if (di) {
        /* Call set_options in any case to cleanly flush the flow
         * caches in the last exporters that are to be destroyed. */
        dpif_ipfix_set_options(
            di, bridge_exporter_options, flow_exporters_options,
            n_flow_exporters_options);

        if (!has_options) {
            dpif_ipfix_unref(di);
            ofproto->ipfix = NULL;
        }
    }

    return 0;
}

static int
set_cfm(struct ofport *ofport_, const struct cfm_settings *s)
{
    struct ofport_dpif *ofport = ofport_dpif_cast(ofport_);
    int error;

    if (!s) {
        error = 0;
    } else {
        if (!ofport->cfm) {
            struct ofproto_dpif *ofproto;

            ofproto = ofproto_dpif_cast(ofport->up.ofproto);
            ofproto->backer->need_revalidate = REV_RECONFIGURE;
            ofport->cfm = cfm_create(ofport->up.netdev);
        }

        if (cfm_configure(ofport->cfm, s)) {
            return 0;
        }

        error = EINVAL;
    }
    cfm_unref(ofport->cfm);
    ofport->cfm = NULL;
    return error;
}

static bool
get_cfm_status(const struct ofport *ofport_,
               struct ofproto_cfm_status *status)
{
    struct ofport_dpif *ofport = ofport_dpif_cast(ofport_);

    if (ofport->cfm) {
        status->faults = cfm_get_fault(ofport->cfm);
        status->remote_opstate = cfm_get_opup(ofport->cfm);
        status->health = cfm_get_health(ofport->cfm);
        cfm_get_remote_mpids(ofport->cfm, &status->rmps, &status->n_rmps);
        return true;
    } else {
        return false;
    }
}

static int
set_bfd(struct ofport *ofport_, const struct smap *cfg)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofport_->ofproto);
    struct ofport_dpif *ofport = ofport_dpif_cast(ofport_);
    struct bfd *old;

    old = ofport->bfd;
    ofport->bfd = bfd_configure(old, netdev_get_name(ofport->up.netdev),
                                cfg, ofport->up.netdev);
    if (ofport->bfd != old) {
        ofproto->backer->need_revalidate = REV_RECONFIGURE;
    }

    return 0;
}

static int
get_bfd_status(struct ofport *ofport_, struct smap *smap)
{
    struct ofport_dpif *ofport = ofport_dpif_cast(ofport_);

    if (ofport->bfd) {
        bfd_get_status(ofport->bfd, smap);
        return 0;
    } else {
        return ENOENT;
    }
}

/* Spanning Tree. */

static void
send_bpdu_cb(struct ofpbuf *pkt, int port_num, void *ofproto_)
{
    struct ofproto_dpif *ofproto = ofproto_;
    struct stp_port *sp = stp_get_port(ofproto->stp, port_num);
    struct ofport_dpif *ofport;

    ofport = stp_port_get_aux(sp);
    if (!ofport) {
        VLOG_WARN_RL(&rl, "%s: cannot send BPDU on unknown port %d",
                     ofproto->up.name, port_num);
    } else {
        struct eth_header *eth = pkt->l2;

        netdev_get_etheraddr(ofport->up.netdev, eth->eth_src);
        if (eth_addr_is_zero(eth->eth_src)) {
            VLOG_WARN_RL(&rl, "%s: cannot send BPDU on port %d "
                         "with unknown MAC", ofproto->up.name, port_num);
        } else {
            send_packet(ofport, pkt);
        }
    }
    ofpbuf_delete(pkt);
}

/* Configures STP on 'ofproto_' using the settings defined in 's'. */
static int
set_stp(struct ofproto *ofproto_, const struct ofproto_stp_settings *s)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);

    /* Only revalidate flows if the configuration changed. */
    if (!s != !ofproto->stp) {
        ofproto->backer->need_revalidate = REV_RECONFIGURE;
    }

    if (s) {
        if (!ofproto->stp) {
            ofproto->stp = stp_create(ofproto_->name, s->system_id,
                                      send_bpdu_cb, ofproto);
            ofproto->stp_last_tick = time_msec();
        }

        stp_set_bridge_id(ofproto->stp, s->system_id);
        stp_set_bridge_priority(ofproto->stp, s->priority);
        stp_set_hello_time(ofproto->stp, s->hello_time);
        stp_set_max_age(ofproto->stp, s->max_age);
        stp_set_forward_delay(ofproto->stp, s->fwd_delay);
    }  else {
        struct ofport *ofport;

        HMAP_FOR_EACH (ofport, hmap_node, &ofproto->up.ports) {
            set_stp_port(ofport, NULL);
        }

        stp_unref(ofproto->stp);
        ofproto->stp = NULL;
    }

    return 0;
}

static int
get_stp_status(struct ofproto *ofproto_, struct ofproto_stp_status *s)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);

    if (ofproto->stp) {
        s->enabled = true;
        s->bridge_id = stp_get_bridge_id(ofproto->stp);
        s->designated_root = stp_get_designated_root(ofproto->stp);
        s->root_path_cost = stp_get_root_path_cost(ofproto->stp);
    } else {
        s->enabled = false;
    }

    return 0;
}

static void
update_stp_port_state(struct ofport_dpif *ofport)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofport->up.ofproto);
    enum stp_state state;

    /* Figure out new state. */
    state = ofport->stp_port ? stp_port_get_state(ofport->stp_port)
                             : STP_DISABLED;

    /* Update state. */
    if (ofport->stp_state != state) {
        enum ofputil_port_state of_state;
        bool fwd_change;

        VLOG_DBG_RL(&rl, "port %s: STP state changed from %s to %s",
                    netdev_get_name(ofport->up.netdev),
                    stp_state_name(ofport->stp_state),
                    stp_state_name(state));
        if (stp_learn_in_state(ofport->stp_state)
                != stp_learn_in_state(state)) {
            /* xxx Learning action flows should also be flushed. */
            ovs_rwlock_wrlock(&ofproto->ml->rwlock);
            mac_learning_flush(ofproto->ml);
            ovs_rwlock_unlock(&ofproto->ml->rwlock);
        }
        fwd_change = stp_forward_in_state(ofport->stp_state)
                        != stp_forward_in_state(state);

        ofproto->backer->need_revalidate = REV_STP;
        ofport->stp_state = state;
        ofport->stp_state_entered = time_msec();

        if (fwd_change && ofport->bundle) {
            bundle_update(ofport->bundle);
        }

        /* Update the STP state bits in the OpenFlow port description. */
        of_state = ofport->up.pp.state & ~OFPUTIL_PS_STP_MASK;
        of_state |= (state == STP_LISTENING ? OFPUTIL_PS_STP_LISTEN
                     : state == STP_LEARNING ? OFPUTIL_PS_STP_LEARN
                     : state == STP_FORWARDING ? OFPUTIL_PS_STP_FORWARD
                     : state == STP_BLOCKING ?  OFPUTIL_PS_STP_BLOCK
                     : 0);
        ofproto_port_set_state(&ofport->up, of_state);
    }
}

/* Configures STP on 'ofport_' using the settings defined in 's'.  The
 * caller is responsible for assigning STP port numbers and ensuring
 * there are no duplicates. */
static int
set_stp_port(struct ofport *ofport_,
             const struct ofproto_port_stp_settings *s)
{
    struct ofport_dpif *ofport = ofport_dpif_cast(ofport_);
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofport->up.ofproto);
    struct stp_port *sp = ofport->stp_port;

    if (!s || !s->enable) {
        if (sp) {
            ofport->stp_port = NULL;
            stp_port_disable(sp);
            update_stp_port_state(ofport);
        }
        return 0;
    } else if (sp && stp_port_no(sp) != s->port_num
            && ofport == stp_port_get_aux(sp)) {
        /* The port-id changed, so disable the old one if it's not
         * already in use by another port. */
        stp_port_disable(sp);
    }

    sp = ofport->stp_port = stp_get_port(ofproto->stp, s->port_num);
    stp_port_enable(sp);

    stp_port_set_aux(sp, ofport);
    stp_port_set_priority(sp, s->priority);
    stp_port_set_path_cost(sp, s->path_cost);

    update_stp_port_state(ofport);

    return 0;
}

static int
get_stp_port_status(struct ofport *ofport_,
                    struct ofproto_port_stp_status *s)
{
    struct ofport_dpif *ofport = ofport_dpif_cast(ofport_);
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofport->up.ofproto);
    struct stp_port *sp = ofport->stp_port;

    if (!ofproto->stp || !sp) {
        s->enabled = false;
        return 0;
    }

    s->enabled = true;
    s->port_id = stp_port_get_id(sp);
    s->state = stp_port_get_state(sp);
    s->sec_in_state = (time_msec() - ofport->stp_state_entered) / 1000;
    s->role = stp_port_get_role(sp);
    stp_port_get_counts(sp, &s->tx_count, &s->rx_count, &s->error_count);

    return 0;
}

static void
stp_run(struct ofproto_dpif *ofproto)
{
    if (ofproto->stp) {
        long long int now = time_msec();
        long long int elapsed = now - ofproto->stp_last_tick;
        struct stp_port *sp;

        if (elapsed > 0) {
            stp_tick(ofproto->stp, MIN(INT_MAX, elapsed));
            ofproto->stp_last_tick = now;
        }
        while (stp_get_changed_port(ofproto->stp, &sp)) {
            struct ofport_dpif *ofport = stp_port_get_aux(sp);

            if (ofport) {
                update_stp_port_state(ofport);
            }
        }

        if (stp_check_and_reset_fdb_flush(ofproto->stp)) {
            ovs_rwlock_wrlock(&ofproto->ml->rwlock);
            mac_learning_flush(ofproto->ml);
            ovs_rwlock_unlock(&ofproto->ml->rwlock);
        }
    }
}

static void
stp_wait(struct ofproto_dpif *ofproto)
{
    if (ofproto->stp) {
        poll_timer_wait(1000);
    }
}

static int
set_queues(struct ofport *ofport_, const struct ofproto_port_queue *qdscp,
           size_t n_qdscp)
{
    struct ofport_dpif *ofport = ofport_dpif_cast(ofport_);
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofport->up.ofproto);

    if (ofport->n_qdscp != n_qdscp
        || (n_qdscp && memcmp(ofport->qdscp, qdscp,
                              n_qdscp * sizeof *qdscp))) {
        ofproto->backer->need_revalidate = REV_RECONFIGURE;
        free(ofport->qdscp);
        ofport->qdscp = n_qdscp
            ? xmemdup(qdscp, n_qdscp * sizeof *qdscp)
            : NULL;
        ofport->n_qdscp = n_qdscp;
    }

    return 0;
}

/* Bundles. */

/* Expires all MAC learning entries associated with 'bundle' and forces its
 * ofproto to revalidate every flow.
 *
 * Normally MAC learning entries are removed only from the ofproto associated
 * with 'bundle', but if 'all_ofprotos' is true, then the MAC learning entries
 * are removed from every ofproto.  When patch ports and SLB bonds are in use
 * and a VM migration happens and the gratuitous ARPs are somehow lost, this
 * avoids a MAC_ENTRY_IDLE_TIME delay before the migrated VM can communicate
 * with the host from which it migrated. */
static void
bundle_flush_macs(struct ofbundle *bundle, bool all_ofprotos)
{
    struct ofproto_dpif *ofproto = bundle->ofproto;
    struct mac_learning *ml = ofproto->ml;
    struct mac_entry *mac, *next_mac;

    ofproto->backer->need_revalidate = REV_RECONFIGURE;
    ovs_rwlock_wrlock(&ml->rwlock);
    LIST_FOR_EACH_SAFE (mac, next_mac, lru_node, &ml->lrus) {
        if (mac->port.p == bundle) {
            if (all_ofprotos) {
                struct ofproto_dpif *o;

                HMAP_FOR_EACH (o, all_ofproto_dpifs_node, &all_ofproto_dpifs) {
                    if (o != ofproto) {
                        struct mac_entry *e;

                        ovs_rwlock_wrlock(&o->ml->rwlock);
                        e = mac_learning_lookup(o->ml, mac->mac, mac->vlan);
                        if (e) {
                            mac_learning_expire(o->ml, e);
                        }
                        ovs_rwlock_unlock(&o->ml->rwlock);
                    }
                }
            }

            mac_learning_expire(ml, mac);
        }
    }
    ovs_rwlock_unlock(&ml->rwlock);
}

static struct ofbundle *
bundle_lookup(const struct ofproto_dpif *ofproto, void *aux)
{
    struct ofbundle *bundle;

    HMAP_FOR_EACH_IN_BUCKET (bundle, hmap_node, hash_pointer(aux, 0),
                             &ofproto->bundles) {
        if (bundle->aux == aux) {
            return bundle;
        }
    }
    return NULL;
}

static void
bundle_update(struct ofbundle *bundle)
{
    struct ofport_dpif *port;

    bundle->floodable = true;
    LIST_FOR_EACH (port, bundle_node, &bundle->ports) {
        if (port->up.pp.config & OFPUTIL_PC_NO_FLOOD
            || !stp_forward_in_state(port->stp_state)) {
            bundle->floodable = false;
            break;
        }
    }
}

static void
bundle_del_port(struct ofport_dpif *port)
{
    struct ofbundle *bundle = port->bundle;

    bundle->ofproto->backer->need_revalidate = REV_RECONFIGURE;

    list_remove(&port->bundle_node);
    port->bundle = NULL;

    if (bundle->lacp) {
        lacp_slave_unregister(bundle->lacp, port);
    }
    if (bundle->bond) {
        bond_slave_unregister(bundle->bond, port);
    }

    bundle_update(bundle);
}

static bool
bundle_add_port(struct ofbundle *bundle, ofp_port_t ofp_port,
                struct lacp_slave_settings *lacp)
{
    struct ofport_dpif *port;

    port = get_ofp_port(bundle->ofproto, ofp_port);
    if (!port) {
        return false;
    }

    if (port->bundle != bundle) {
        bundle->ofproto->backer->need_revalidate = REV_RECONFIGURE;
        if (port->bundle) {
            bundle_remove(&port->up);
        }

        port->bundle = bundle;
        list_push_back(&bundle->ports, &port->bundle_node);
        if (port->up.pp.config & OFPUTIL_PC_NO_FLOOD
            || !stp_forward_in_state(port->stp_state)) {
            bundle->floodable = false;
        }
    }
    if (lacp) {
        bundle->ofproto->backer->need_revalidate = REV_RECONFIGURE;
        lacp_slave_register(bundle->lacp, port, lacp);
    }

    return true;
}

static void
bundle_destroy(struct ofbundle *bundle)
{
    struct ofproto_dpif *ofproto;
    struct ofport_dpif *port, *next_port;

    if (!bundle) {
        return;
    }

    ofproto = bundle->ofproto;
    mbridge_unregister_bundle(ofproto->mbridge, bundle->aux);

    ovs_rwlock_wrlock(&xlate_rwlock);
    xlate_bundle_remove(bundle);
    ovs_rwlock_unlock(&xlate_rwlock);

    LIST_FOR_EACH_SAFE (port, next_port, bundle_node, &bundle->ports) {
        bundle_del_port(port);
    }

    bundle_flush_macs(bundle, true);
    hmap_remove(&ofproto->bundles, &bundle->hmap_node);
    free(bundle->name);
    free(bundle->trunks);
    lacp_unref(bundle->lacp);
    bond_unref(bundle->bond);
    free(bundle);
}

static int
bundle_set(struct ofproto *ofproto_, void *aux,
           const struct ofproto_bundle_settings *s)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    bool need_flush = false;
    struct ofport_dpif *port;
    struct ofbundle *bundle;
    unsigned long *trunks;
    int vlan;
    size_t i;
    bool ok;

    if (!s) {
        bundle_destroy(bundle_lookup(ofproto, aux));
        return 0;
    }

    ovs_assert(s->n_slaves == 1 || s->bond != NULL);
    ovs_assert((s->lacp != NULL) == (s->lacp_slaves != NULL));

    bundle = bundle_lookup(ofproto, aux);
    if (!bundle) {
        bundle = xmalloc(sizeof *bundle);

        bundle->ofproto = ofproto;
        hmap_insert(&ofproto->bundles, &bundle->hmap_node,
                    hash_pointer(aux, 0));
        bundle->aux = aux;
        bundle->name = NULL;

        list_init(&bundle->ports);
        bundle->vlan_mode = PORT_VLAN_TRUNK;
        bundle->vlan = -1;
        bundle->trunks = NULL;
        bundle->use_priority_tags = s->use_priority_tags;
        bundle->lacp = NULL;
        bundle->bond = NULL;

        bundle->floodable = true;
        mbridge_register_bundle(ofproto->mbridge, bundle);
    }

    if (!bundle->name || strcmp(s->name, bundle->name)) {
        free(bundle->name);
        bundle->name = xstrdup(s->name);
    }

    /* LACP. */
    if (s->lacp) {
        if (!bundle->lacp) {
            ofproto->backer->need_revalidate = REV_RECONFIGURE;
            bundle->lacp = lacp_create();
        }
        lacp_configure(bundle->lacp, s->lacp);
    } else {
        lacp_unref(bundle->lacp);
        bundle->lacp = NULL;
    }

    /* Update set of ports. */
    ok = true;
    for (i = 0; i < s->n_slaves; i++) {
        if (!bundle_add_port(bundle, s->slaves[i],
                             s->lacp ? &s->lacp_slaves[i] : NULL)) {
            ok = false;
        }
    }
    if (!ok || list_size(&bundle->ports) != s->n_slaves) {
        struct ofport_dpif *next_port;

        LIST_FOR_EACH_SAFE (port, next_port, bundle_node, &bundle->ports) {
            for (i = 0; i < s->n_slaves; i++) {
                if (s->slaves[i] == port->up.ofp_port) {
                    goto found;
                }
            }

            bundle_del_port(port);
        found: ;
        }
    }
    ovs_assert(list_size(&bundle->ports) <= s->n_slaves);

    if (list_is_empty(&bundle->ports)) {
        bundle_destroy(bundle);
        return EINVAL;
    }

    /* Set VLAN tagging mode */
    if (s->vlan_mode != bundle->vlan_mode
        || s->use_priority_tags != bundle->use_priority_tags) {
        bundle->vlan_mode = s->vlan_mode;
        bundle->use_priority_tags = s->use_priority_tags;
        need_flush = true;
    }

    /* Set VLAN tag. */
    vlan = (s->vlan_mode == PORT_VLAN_TRUNK ? -1
            : s->vlan >= 0 && s->vlan <= 4095 ? s->vlan
            : 0);
    if (vlan != bundle->vlan) {
        bundle->vlan = vlan;
        need_flush = true;
    }

    /* Get trunked VLANs. */
    switch (s->vlan_mode) {
    case PORT_VLAN_ACCESS:
        trunks = NULL;
        break;

    case PORT_VLAN_TRUNK:
        trunks = CONST_CAST(unsigned long *, s->trunks);
        break;

    case PORT_VLAN_NATIVE_UNTAGGED:
    case PORT_VLAN_NATIVE_TAGGED:
        if (vlan != 0 && (!s->trunks
                          || !bitmap_is_set(s->trunks, vlan)
                          || bitmap_is_set(s->trunks, 0))) {
            /* Force trunking the native VLAN and prohibit trunking VLAN 0. */
            if (s->trunks) {
                trunks = bitmap_clone(s->trunks, 4096);
            } else {
                trunks = bitmap_allocate1(4096);
            }
            bitmap_set1(trunks, vlan);
            bitmap_set0(trunks, 0);
        } else {
            trunks = CONST_CAST(unsigned long *, s->trunks);
        }
        break;

    default:
        NOT_REACHED();
    }
    if (!vlan_bitmap_equal(trunks, bundle->trunks)) {
        free(bundle->trunks);
        if (trunks == s->trunks) {
            bundle->trunks = vlan_bitmap_clone(trunks);
        } else {
            bundle->trunks = trunks;
            trunks = NULL;
        }
        need_flush = true;
    }
    if (trunks != s->trunks) {
        free(trunks);
    }

    /* Bonding. */
    if (!list_is_short(&bundle->ports)) {
        bundle->ofproto->has_bonded_bundles = true;
        if (bundle->bond) {
            if (bond_reconfigure(bundle->bond, s->bond)) {
                ofproto->backer->need_revalidate = REV_RECONFIGURE;
            }
        } else {
            bundle->bond = bond_create(s->bond);
            ofproto->backer->need_revalidate = REV_RECONFIGURE;
        }

        LIST_FOR_EACH (port, bundle_node, &bundle->ports) {
            bond_slave_register(bundle->bond, port, port->up.netdev);
        }
    } else {
        bond_unref(bundle->bond);
        bundle->bond = NULL;
    }

    /* If we changed something that would affect MAC learning, un-learn
     * everything on this port and force flow revalidation. */
    if (need_flush) {
        bundle_flush_macs(bundle, false);
    }

    return 0;
}

static void
bundle_remove(struct ofport *port_)
{
    struct ofport_dpif *port = ofport_dpif_cast(port_);
    struct ofbundle *bundle = port->bundle;

    if (bundle) {
        bundle_del_port(port);
        if (list_is_empty(&bundle->ports)) {
            bundle_destroy(bundle);
        } else if (list_is_short(&bundle->ports)) {
            bond_unref(bundle->bond);
            bundle->bond = NULL;
        }
    }
}

static void
send_pdu_cb(void *port_, const void *pdu, size_t pdu_size)
{
    static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 10);
    struct ofport_dpif *port = port_;
    uint8_t ea[ETH_ADDR_LEN];
    int error;

    error = netdev_get_etheraddr(port->up.netdev, ea);
    if (!error) {
        struct ofpbuf packet;
        void *packet_pdu;

        ofpbuf_init(&packet, 0);
        packet_pdu = eth_compose(&packet, eth_addr_lacp, ea, ETH_TYPE_LACP,
                                 pdu_size);
        memcpy(packet_pdu, pdu, pdu_size);

        send_packet(port, &packet);
        ofpbuf_uninit(&packet);
    } else {
        VLOG_ERR_RL(&rl, "port %s: cannot obtain Ethernet address of iface "
                    "%s (%s)", port->bundle->name,
                    netdev_get_name(port->up.netdev), ovs_strerror(error));
    }
}

static void
bundle_send_learning_packets(struct ofbundle *bundle)
{
    struct ofproto_dpif *ofproto = bundle->ofproto;
    struct ofpbuf *learning_packet;
    int error, n_packets, n_errors;
    struct mac_entry *e;
    struct clist packets;

    list_init(&packets);
    ovs_rwlock_rdlock(&ofproto->ml->rwlock);
    LIST_FOR_EACH (e, lru_node, &ofproto->ml->lrus) {
        if (e->port.p != bundle) {
            void *port_void;

            learning_packet = bond_compose_learning_packet(bundle->bond,
                                                           e->mac, e->vlan,
                                                           &port_void);
            learning_packet->private_p = port_void;
            list_push_back(&packets, &learning_packet->list_node);
        }
    }
    ovs_rwlock_unlock(&ofproto->ml->rwlock);

    error = n_packets = n_errors = 0;
    LIST_FOR_EACH (learning_packet, list_node, &packets) {
        int ret;

        ret = send_packet(learning_packet->private_p, learning_packet);
        if (ret) {
            error = ret;
            n_errors++;
        }
        n_packets++;
    }
    ofpbuf_list_delete(&packets);

    if (n_errors) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
        VLOG_WARN_RL(&rl, "bond %s: %d errors sending %d gratuitous learning "
                     "packets, last error was: %s",
                     bundle->name, n_errors, n_packets, ovs_strerror(error));
    } else {
        VLOG_DBG("bond %s: sent %d gratuitous learning packets",
                 bundle->name, n_packets);
    }
}

static void
bundle_run(struct ofbundle *bundle)
{
    if (bundle->lacp) {
        lacp_run(bundle->lacp, send_pdu_cb);
    }
    if (bundle->bond) {
        struct ofport_dpif *port;

        LIST_FOR_EACH (port, bundle_node, &bundle->ports) {
            bond_slave_set_may_enable(bundle->bond, port, port->may_enable);
        }

        if (bond_run(bundle->bond, lacp_status(bundle->lacp))) {
            bundle->ofproto->backer->need_revalidate = REV_BOND;
        }

        if (bond_should_send_learning_packets(bundle->bond)) {
            bundle_send_learning_packets(bundle);
        }
    }
}

static void
bundle_wait(struct ofbundle *bundle)
{
    if (bundle->lacp) {
        lacp_wait(bundle->lacp);
    }
    if (bundle->bond) {
        bond_wait(bundle->bond);
    }
}

/* Mirrors. */

static int
mirror_set__(struct ofproto *ofproto_, void *aux,
             const struct ofproto_mirror_settings *s)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    struct ofbundle **srcs, **dsts;
    int error;
    size_t i;

    if (!s) {
        mirror_destroy(ofproto->mbridge, aux);
        return 0;
    }

    srcs = xmalloc(s->n_srcs * sizeof *srcs);
    dsts = xmalloc(s->n_dsts * sizeof *dsts);

    for (i = 0; i < s->n_srcs; i++) {
        srcs[i] = bundle_lookup(ofproto, s->srcs[i]);
    }

    for (i = 0; i < s->n_dsts; i++) {
        dsts[i] = bundle_lookup(ofproto, s->dsts[i]);
    }

    error = mirror_set(ofproto->mbridge, aux, s->name, srcs, s->n_srcs, dsts,
                       s->n_dsts, s->src_vlans,
                       bundle_lookup(ofproto, s->out_bundle), s->out_vlan);
    free(srcs);
    free(dsts);
    return error;
}

static int
mirror_get_stats__(struct ofproto *ofproto, void *aux,
                   uint64_t *packets, uint64_t *bytes)
{
    push_all_stats();
    return mirror_get_stats(ofproto_dpif_cast(ofproto)->mbridge, aux, packets,
                            bytes);
}

static int
set_flood_vlans(struct ofproto *ofproto_, unsigned long *flood_vlans)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    ovs_rwlock_wrlock(&ofproto->ml->rwlock);
    if (mac_learning_set_flood_vlans(ofproto->ml, flood_vlans)) {
        mac_learning_flush(ofproto->ml);
    }
    ovs_rwlock_unlock(&ofproto->ml->rwlock);
    return 0;
}

static bool
is_mirror_output_bundle(const struct ofproto *ofproto_, void *aux)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    struct ofbundle *bundle = bundle_lookup(ofproto, aux);
    return bundle && mirror_bundle_out(ofproto->mbridge, bundle) != 0;
}

static void
forward_bpdu_changed(struct ofproto *ofproto_)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    ofproto->backer->need_revalidate = REV_RECONFIGURE;
}

static void
set_mac_table_config(struct ofproto *ofproto_, unsigned int idle_time,
                     size_t max_entries)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    ovs_rwlock_wrlock(&ofproto->ml->rwlock);
    mac_learning_set_idle_time(ofproto->ml, idle_time);
    mac_learning_set_max_entries(ofproto->ml, max_entries);
    ovs_rwlock_unlock(&ofproto->ml->rwlock);
}

/* Ports. */

static struct ofport_dpif *
get_ofp_port(const struct ofproto_dpif *ofproto, ofp_port_t ofp_port)
{
    struct ofport *ofport = ofproto_get_port(&ofproto->up, ofp_port);
    return ofport ? ofport_dpif_cast(ofport) : NULL;
}

static struct ofport_dpif *
get_odp_port(const struct ofproto_dpif *ofproto, odp_port_t odp_port)
{
    struct ofport_dpif *port = odp_port_to_ofport(ofproto->backer, odp_port);
    return port && &ofproto->up == port->up.ofproto ? port : NULL;
}

static void
ofproto_port_from_dpif_port(struct ofproto_dpif *ofproto,
                            struct ofproto_port *ofproto_port,
                            struct dpif_port *dpif_port)
{
    ofproto_port->name = dpif_port->name;
    ofproto_port->type = dpif_port->type;
    ofproto_port->ofp_port = odp_port_to_ofp_port(ofproto, dpif_port->port_no);
}

static void
ofport_update_peer(struct ofport_dpif *ofport)
{
    const struct ofproto_dpif *ofproto;
    struct dpif_backer *backer;
    char *peer_name;

    if (!netdev_vport_is_patch(ofport->up.netdev)) {
        return;
    }

    backer = ofproto_dpif_cast(ofport->up.ofproto)->backer;
    backer->need_revalidate = REV_RECONFIGURE;

    if (ofport->peer) {
        ofport->peer->peer = NULL;
        ofport->peer = NULL;
    }

    peer_name = netdev_vport_patch_peer(ofport->up.netdev);
    if (!peer_name) {
        return;
    }

    HMAP_FOR_EACH (ofproto, all_ofproto_dpifs_node, &all_ofproto_dpifs) {
        struct ofport *peer_ofport;
        struct ofport_dpif *peer;
        char *peer_peer;

        if (ofproto->backer != backer) {
            continue;
        }

        peer_ofport = shash_find_data(&ofproto->up.port_by_name, peer_name);
        if (!peer_ofport) {
            continue;
        }

        peer = ofport_dpif_cast(peer_ofport);
        peer_peer = netdev_vport_patch_peer(peer->up.netdev);
        if (peer_peer && !strcmp(netdev_get_name(ofport->up.netdev),
                                 peer_peer)) {
            ofport->peer = peer;
            ofport->peer->peer = ofport;
        }
        free(peer_peer);

        break;
    }
    free(peer_name);
}

static void
port_run_fast(struct ofport_dpif *ofport)
{
    if (ofport->cfm && cfm_should_send_ccm(ofport->cfm)) {
        struct ofpbuf packet;

        ofpbuf_init(&packet, 0);
        cfm_compose_ccm(ofport->cfm, &packet, ofport->up.pp.hw_addr);
        send_packet(ofport, &packet);
        ofpbuf_uninit(&packet);
    }

    if (ofport->bfd && bfd_should_send_packet(ofport->bfd)) {
        struct ofpbuf packet;

        ofpbuf_init(&packet, 0);
        bfd_put_packet(ofport->bfd, &packet, ofport->up.pp.hw_addr);
        send_packet(ofport, &packet);
        ofpbuf_uninit(&packet);
    }
}

static void
port_run(struct ofport_dpif *ofport)
{
    long long int carrier_seq = netdev_get_carrier_resets(ofport->up.netdev);
    bool carrier_changed = carrier_seq != ofport->carrier_seq;
    bool enable = netdev_get_carrier(ofport->up.netdev);
    bool cfm_enable = false;
    bool bfd_enable = false;

    ofport->carrier_seq = carrier_seq;

    port_run_fast(ofport);

    if (ofport->cfm) {
        int cfm_opup = cfm_get_opup(ofport->cfm);

        cfm_run(ofport->cfm);
        cfm_enable = !cfm_get_fault(ofport->cfm);

        if (cfm_opup >= 0) {
            cfm_enable = cfm_enable && cfm_opup;
        }
    }

    if (ofport->bfd) {
        bfd_run(ofport->bfd);
        bfd_enable = bfd_forwarding(ofport->bfd);
    }

    if (ofport->bfd || ofport->cfm) {
        enable = enable && (cfm_enable || bfd_enable);
    }

    if (ofport->bundle) {
        enable = enable && lacp_slave_may_enable(ofport->bundle->lacp, ofport);
        if (carrier_changed) {
            lacp_slave_carrier_changed(ofport->bundle->lacp, ofport);
        }
    }

    if (ofport->may_enable != enable) {
        struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofport->up.ofproto);
        ofproto->backer->need_revalidate = REV_PORT_TOGGLED;
    }

    ofport->may_enable = enable;
}

static void
port_wait(struct ofport_dpif *ofport)
{
    if (ofport->cfm) {
        cfm_wait(ofport->cfm);
    }

    if (ofport->bfd) {
        bfd_wait(ofport->bfd);
    }
}

static int
port_query_by_name(const struct ofproto *ofproto_, const char *devname,
                   struct ofproto_port *ofproto_port)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    struct dpif_port dpif_port;
    int error;

    if (sset_contains(&ofproto->ghost_ports, devname)) {
        const char *type = netdev_get_type_from_name(devname);

        /* We may be called before ofproto->up.port_by_name is populated with
         * the appropriate ofport.  For this reason, we must get the name and
         * type from the netdev layer directly. */
        if (type) {
            const struct ofport *ofport;

            ofport = shash_find_data(&ofproto->up.port_by_name, devname);
            ofproto_port->ofp_port = ofport ? ofport->ofp_port : OFPP_NONE;
            ofproto_port->name = xstrdup(devname);
            ofproto_port->type = xstrdup(type);
            return 0;
        }
        return ENODEV;
    }

    if (!sset_contains(&ofproto->ports, devname)) {
        return ENODEV;
    }
    error = dpif_port_query_by_name(ofproto->backer->dpif,
                                    devname, &dpif_port);
    if (!error) {
        ofproto_port_from_dpif_port(ofproto, ofproto_port, &dpif_port);
    }
    return error;
}

static int
port_add(struct ofproto *ofproto_, struct netdev *netdev)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    const char *devname = netdev_get_name(netdev);
    char namebuf[NETDEV_VPORT_NAME_BUFSIZE];
    const char *dp_port_name;

    if (netdev_vport_is_patch(netdev)) {
        sset_add(&ofproto->ghost_ports, netdev_get_name(netdev));
        return 0;
    }

    dp_port_name = netdev_vport_get_dpif_port(netdev, namebuf, sizeof namebuf);
    if (!dpif_port_exists(ofproto->backer->dpif, dp_port_name)) {
        odp_port_t port_no = ODPP_NONE;
        int error;

        error = dpif_port_add(ofproto->backer->dpif, netdev, &port_no);
        if (error) {
            return error;
        }
        if (netdev_get_tunnel_config(netdev)) {
            simap_put(&ofproto->backer->tnl_backers,
                      dp_port_name, odp_to_u32(port_no));
        }
    }

    if (netdev_get_tunnel_config(netdev)) {
        sset_add(&ofproto->ghost_ports, devname);
    } else {
        sset_add(&ofproto->ports, devname);
    }
    return 0;
}

static int
port_del(struct ofproto *ofproto_, ofp_port_t ofp_port)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    struct ofport_dpif *ofport = get_ofp_port(ofproto, ofp_port);
    int error = 0;

    if (!ofport) {
        return 0;
    }

    sset_find_and_delete(&ofproto->ghost_ports,
                         netdev_get_name(ofport->up.netdev));
    ofproto->backer->need_revalidate = REV_RECONFIGURE;
    if (!ofport->is_tunnel && !netdev_vport_is_patch(ofport->up.netdev)) {
        error = dpif_port_del(ofproto->backer->dpif, ofport->odp_port);
        if (!error) {
            /* The caller is going to close ofport->up.netdev.  If this is a
             * bonded port, then the bond is using that netdev, so remove it
             * from the bond.  The client will need to reconfigure everything
             * after deleting ports, so then the slave will get re-added. */
            bundle_remove(&ofport->up);
        }
    }
    return error;
}

static int
port_get_stats(const struct ofport *ofport_, struct netdev_stats *stats)
{
    struct ofport_dpif *ofport = ofport_dpif_cast(ofport_);
    int error;

    push_all_stats();

    error = netdev_get_stats(ofport->up.netdev, stats);

    if (!error && ofport_->ofp_port == OFPP_LOCAL) {
        struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofport->up.ofproto);

        /* ofproto->stats.tx_packets represents packets that we created
         * internally and sent to some port (e.g. packets sent with
         * send_packet()).  Account for them as if they had come from
         * OFPP_LOCAL and got forwarded. */

        if (stats->rx_packets != UINT64_MAX) {
            stats->rx_packets += ofproto->stats.tx_packets;
        }

        if (stats->rx_bytes != UINT64_MAX) {
            stats->rx_bytes += ofproto->stats.tx_bytes;
        }

        /* ofproto->stats.rx_packets represents packets that were received on
         * some port and we processed internally and dropped (e.g. STP).
         * Account for them as if they had been forwarded to OFPP_LOCAL. */

        if (stats->tx_packets != UINT64_MAX) {
            stats->tx_packets += ofproto->stats.rx_packets;
        }

        if (stats->tx_bytes != UINT64_MAX) {
            stats->tx_bytes += ofproto->stats.rx_bytes;
        }
    }

    return error;
}

struct port_dump_state {
    uint32_t bucket;
    uint32_t offset;
    bool ghost;

    struct ofproto_port port;
    bool has_port;
};

static int
port_dump_start(const struct ofproto *ofproto_ OVS_UNUSED, void **statep)
{
    *statep = xzalloc(sizeof(struct port_dump_state));
    return 0;
}

static int
port_dump_next(const struct ofproto *ofproto_, void *state_,
               struct ofproto_port *port)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    struct port_dump_state *state = state_;
    const struct sset *sset;
    struct sset_node *node;

    if (state->has_port) {
        ofproto_port_destroy(&state->port);
        state->has_port = false;
    }
    sset = state->ghost ? &ofproto->ghost_ports : &ofproto->ports;
    while ((node = sset_at_position(sset, &state->bucket, &state->offset))) {
        int error;

        error = port_query_by_name(ofproto_, node->name, &state->port);
        if (!error) {
            *port = state->port;
            state->has_port = true;
            return 0;
        } else if (error != ENODEV) {
            return error;
        }
    }

    if (!state->ghost) {
        state->ghost = true;
        state->bucket = 0;
        state->offset = 0;
        return port_dump_next(ofproto_, state_, port);
    }

    return EOF;
}

static int
port_dump_done(const struct ofproto *ofproto_ OVS_UNUSED, void *state_)
{
    struct port_dump_state *state = state_;

    if (state->has_port) {
        ofproto_port_destroy(&state->port);
    }
    free(state);
    return 0;
}

static int
port_poll(const struct ofproto *ofproto_, char **devnamep)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);

    if (ofproto->port_poll_errno) {
        int error = ofproto->port_poll_errno;
        ofproto->port_poll_errno = 0;
        return error;
    }

    if (sset_is_empty(&ofproto->port_poll_set)) {
        return EAGAIN;
    }

    *devnamep = sset_pop(&ofproto->port_poll_set);
    return 0;
}

static void
port_poll_wait(const struct ofproto *ofproto_)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    dpif_port_poll_wait(ofproto->backer->dpif);
}

static int
port_is_lacp_current(const struct ofport *ofport_)
{
    const struct ofport_dpif *ofport = ofport_dpif_cast(ofport_);
    return (ofport->bundle && ofport->bundle->lacp
            ? lacp_slave_is_current(ofport->bundle->lacp, ofport)
            : -1);
}

/* Upcall handling. */

struct flow_miss_op {
    struct dpif_op dpif_op;

    uint64_t slow_stub[128 / 8]; /* Buffer for compose_slow_path() */
    struct xlate_out xout;
    bool xout_garbage;           /* 'xout' needs to be uninitialized? */

    struct ofpbuf mask;          /* Flow mask for "put" ops. */
    struct odputil_keybuf maskbuf;

    /* If this is a "put" op, then a pointer to the subfacet that should
     * be marked as uninstalled if the operation fails. */
    struct subfacet *subfacet;
};

/* Figures out whether a flow that missed in 'ofproto', whose details are in
 * 'miss' masked by 'wc', is likely to be worth tracking in detail in userspace
 * and (usually) installing a datapath flow.  The answer is usually "yes" (a
 * return value of true).  However, for short flows the cost of bookkeeping is
 * much higher than the benefits, so when the datapath holds a large number of
 * flows we impose some heuristics to decide which flows are likely to be worth
 * tracking. */
static bool
flow_miss_should_make_facet(struct flow_miss *miss)
{
    struct dpif_backer *backer = miss->ofproto->backer;
    uint32_t hash;

    switch (flow_miss_model) {
    case OFPROTO_HANDLE_MISS_AUTO:
        break;
    case OFPROTO_HANDLE_MISS_WITH_FACETS:
        return true;
    case OFPROTO_HANDLE_MISS_WITHOUT_FACETS:
        return false;
    }

    if (!backer->governor) {
        size_t n_subfacets;

        n_subfacets = hmap_count(&backer->subfacets);
        if (n_subfacets * 2 <= flow_eviction_threshold) {
            return true;
        }

        backer->governor = governor_create();
    }

    hash = flow_hash_in_wildcards(&miss->flow, &miss->xout.wc, 0);
    return governor_should_install_flow(backer->governor, hash,
                                        list_size(&miss->packets));
}

/* Handles 'miss', which matches 'facet'.  May add any required datapath
 * operations to 'ops', incrementing '*n_ops' for each new op.
 *
 * All of the packets in 'miss' are considered to have arrived at time
 * 'miss->stats.used'.  This is really important only for new facets: if we
 * just called time_msec() here, then the new subfacet or its packets could
 * look (occasionally) as though it was used some time after the facet was
 * used.  That can make a one-packet flow look like it has a nonzero duration,
 * which looks odd in e.g. NetFlow statistics. */
static void
handle_flow_miss_with_facet(struct flow_miss *miss, struct facet *facet,
                            struct flow_miss_op *ops, size_t *n_ops)
{
    enum subfacet_path want_path;
    struct subfacet *subfacet;

    facet->packet_count += miss->stats.n_packets;
    facet->prev_packet_count += miss->stats.n_packets;
    facet->byte_count += miss->stats.n_bytes;
    facet->prev_byte_count += miss->stats.n_bytes;

    want_path = facet->xout.slow ? SF_SLOW_PATH : SF_FAST_PATH;

    /* Don't install the flow if it's the result of the "userspace"
     * action for an already installed facet.  This can occur when a
     * datapath flow with wildcards has a "userspace" action and flows
     * sent to userspace result in a different subfacet, which will then
     * be rejected as overlapping by the datapath. */
    if (miss->upcall_type == DPIF_UC_ACTION
        && !list_is_empty(&facet->subfacets)) {
        return;
    }

    subfacet = subfacet_create(facet, miss);
    if (subfacet->path != want_path) {
        struct flow_miss_op *op = &ops[(*n_ops)++];
        struct dpif_flow_put *put = &op->dpif_op.u.flow_put;

        subfacet->path = want_path;

        ofpbuf_use_stack(&op->mask, &op->maskbuf, sizeof op->maskbuf);
        if (enable_megaflows) {
            odp_flow_key_from_mask(&op->mask, &facet->xout.wc.masks,
                                   &miss->flow, UINT32_MAX);
        }

        op->xout_garbage = false;
        op->dpif_op.type = DPIF_OP_FLOW_PUT;
        op->subfacet = subfacet;
        put->flags = DPIF_FP_CREATE;
        put->key = miss->key;
        put->key_len = miss->key_len;
        put->mask = op->mask.data;
        put->mask_len = op->mask.size;

        if (want_path == SF_FAST_PATH) {
            put->actions = facet->xout.odp_actions.data;
            put->actions_len = facet->xout.odp_actions.size;
        } else {
            compose_slow_path(facet->ofproto, &miss->flow, facet->xout.slow,
                              op->slow_stub, sizeof op->slow_stub,
                              &put->actions, &put->actions_len);
        }
        put->stats = NULL;
    }
}

/* Handles flow miss 'miss'.  May add any required datapath operations
 * to 'ops', incrementing '*n_ops' for each new op. */
static void
handle_flow_miss(struct flow_miss *miss, struct flow_miss_op *ops,
                 size_t *n_ops)
{
    struct facet *facet;

    miss->ofproto->n_missed += list_size(&miss->packets);

    facet = facet_lookup_valid(miss->ofproto, &miss->flow);
    if (!facet) {
        /* There does not exist a bijection between 'struct flow' and datapath
         * flow keys with fitness ODP_FIT_TO_LITTLE.  This breaks a fundamental
         * assumption used throughout the facet and subfacet handling code.
         * Since we have to handle these misses in userspace anyway, we simply
         * skip facet creation, avoiding the problem altogether. */
        if (miss->key_fitness == ODP_FIT_TOO_LITTLE
            || !flow_miss_should_make_facet(miss)) {
            return;
        }

        facet = facet_create(miss);
    }
    handle_flow_miss_with_facet(miss, facet, ops, n_ops);
}

static struct drop_key *
drop_key_lookup(const struct dpif_backer *backer, const struct nlattr *key,
                size_t key_len)
{
    struct drop_key *drop_key;

    HMAP_FOR_EACH_WITH_HASH (drop_key, hmap_node, hash_bytes(key, key_len, 0),
                             &backer->drop_keys) {
        if (drop_key->key_len == key_len
            && !memcmp(drop_key->key, key, key_len)) {
            return drop_key;
        }
    }
    return NULL;
}

static void
drop_key_clear(struct dpif_backer *backer)
{
    static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 15);
    struct drop_key *drop_key, *next;

    HMAP_FOR_EACH_SAFE (drop_key, next, hmap_node, &backer->drop_keys) {
        int error;

        error = dpif_flow_del(backer->dpif, drop_key->key, drop_key->key_len,
                              NULL);
        if (error && !VLOG_DROP_WARN(&rl)) {
            struct ds ds = DS_EMPTY_INITIALIZER;
            odp_flow_key_format(drop_key->key, drop_key->key_len, &ds);
            VLOG_WARN("Failed to delete drop key (%s) (%s)",
                      ovs_strerror(error), ds_cstr(&ds));
            ds_destroy(&ds);
        }

        hmap_remove(&backer->drop_keys, &drop_key->hmap_node);
        drop_key_destroy(drop_key);
    }

    udpif_drop_key_clear(backer->udpif);
}

static void
handle_flow_misses(struct dpif_backer *backer, struct flow_miss_batch *fmb)
{
    struct flow_miss_op flow_miss_ops[FLOW_MISS_MAX_BATCH];
    struct dpif_op *dpif_ops[FLOW_MISS_MAX_BATCH];
    struct flow_miss *miss;
    size_t n_ops, i;

    /* Process each element in the to-do list, constructing the set of
     * operations to batch. */
    n_ops = 0;
    HMAP_FOR_EACH (miss, hmap_node, &fmb->misses) {
        handle_flow_miss(miss, flow_miss_ops, &n_ops);
    }
    ovs_assert(n_ops <= ARRAY_SIZE(flow_miss_ops));

    /* Execute batch. */
    for (i = 0; i < n_ops; i++) {
        dpif_ops[i] = &flow_miss_ops[i].dpif_op;
    }
    dpif_operate(backer->dpif, dpif_ops, n_ops);

    for (i = 0; i < n_ops; i++) {
        if (dpif_ops[i]->error != 0
            && flow_miss_ops[i].dpif_op.type == DPIF_OP_FLOW_PUT
            && flow_miss_ops[i].subfacet) {
            struct subfacet *subfacet = flow_miss_ops[i].subfacet;

            COVERAGE_INC(subfacet_install_fail);

            /* Zero-out subfacet counters when installation failed, but
             * datapath reported hits.  This should not happen and
             * indicates a bug, since if the datapath flow exists, we
             * should not be attempting to create a new subfacet.  A
             * buggy datapath could trigger this, so just zero out the
             * counters and log an error. */
            if (subfacet->dp_packet_count || subfacet->dp_byte_count) {
                VLOG_ERR_RL(&rl, "failed to install subfacet for which "
                            "datapath reported hits");
                subfacet->dp_packet_count = subfacet->dp_byte_count = 0;
            }

            subfacet->path = SF_NOT_INSTALLED;
        }
    }
}

static void
handle_sflow_upcall(struct dpif_backer *backer,
                    const struct dpif_upcall *upcall)
{
    struct ofproto_dpif *ofproto;
    union user_action_cookie cookie;
    struct flow flow;
    odp_port_t odp_in_port;

    if (xlate_receive(backer, upcall->packet, upcall->key, upcall->key_len,
                      &flow, NULL, &ofproto, &odp_in_port)
        || !ofproto->sflow) {
        return;
    }

    memset(&cookie, 0, sizeof cookie);
    memcpy(&cookie, nl_attr_get(upcall->userdata), sizeof cookie.sflow);
    dpif_sflow_received(ofproto->sflow, upcall->packet, &flow,
                        odp_in_port, &cookie);
}

static void
handle_flow_sample_upcall(struct dpif_backer *backer,
                          const struct dpif_upcall *upcall)
{
    struct ofproto_dpif *ofproto;
    union user_action_cookie cookie;
    struct flow flow;

    if (xlate_receive(backer, upcall->packet, upcall->key, upcall->key_len,
                      &flow, NULL, &ofproto, NULL)
        || !ofproto->ipfix) {
        return;
    }

    memset(&cookie, 0, sizeof cookie);
    memcpy(&cookie, nl_attr_get(upcall->userdata), sizeof cookie.flow_sample);

    /* The flow reflects exactly the contents of the packet.  Sample
     * the packet using it. */
    dpif_ipfix_flow_sample(ofproto->ipfix, upcall->packet, &flow,
                           cookie.flow_sample.collector_set_id,
                           cookie.flow_sample.probability,
                           cookie.flow_sample.obs_domain_id,
                           cookie.flow_sample.obs_point_id);
}

static void
handle_ipfix_upcall(struct dpif_backer *backer,
                    const struct dpif_upcall *upcall)
{
    struct ofproto_dpif *ofproto;
    struct flow flow;

    if (xlate_receive(backer, upcall->packet, upcall->key, upcall->key_len,
                      &flow, NULL, &ofproto, NULL)
        || !ofproto->ipfix) {
        return;
    }

    /* The flow reflects exactly the contents of the packet.  Sample
     * the packet using it. */
    dpif_ipfix_bridge_sample(ofproto->ipfix, upcall->packet, &flow);
}

static void
handle_upcalls(struct dpif_backer *backer)
{
    struct flow_miss_batch *fmb;
    int n_processed;

    for (n_processed = 0; n_processed < FLOW_MISS_MAX_BATCH; n_processed++) {
        struct upcall *upcall = upcall_next(backer->udpif);

        if (!upcall) {
            break;
        }

        switch (upcall->type) {
        case SFLOW_UPCALL:
            handle_sflow_upcall(backer, &upcall->dpif_upcall);
            break;

        case FLOW_SAMPLE_UPCALL:
            handle_flow_sample_upcall(backer, &upcall->dpif_upcall);
            break;

        case IPFIX_UPCALL:
            handle_ipfix_upcall(backer, &upcall->dpif_upcall);
            break;

        case BAD_UPCALL:
            break;

        case MISS_UPCALL:
            NOT_REACHED();
        }

        upcall_destroy(upcall);
    }

    for (n_processed = 0; n_processed < FLOW_MISS_MAX_BATCH; n_processed++) {
        struct drop_key *drop_key = drop_key_next(backer->udpif);
        if (!drop_key) {
            break;
        }

        if (!drop_key_lookup(backer, drop_key->key, drop_key->key_len)) {
            hmap_insert(&backer->drop_keys, &drop_key->hmap_node,
                        hash_bytes(drop_key->key, drop_key->key_len, 0));
            dpif_flow_put(backer->dpif, DPIF_FP_CREATE | DPIF_FP_MODIFY,
                          drop_key->key, drop_key->key_len,
                          NULL, 0, NULL, 0, NULL);
        } else {
            drop_key_destroy(drop_key);
        }
    }

    fmb = flow_miss_batch_next(backer->udpif);
    if (fmb) {
        handle_flow_misses(backer, fmb);
        flow_miss_batch_destroy(fmb);
    }
}

/* Flow expiration. */

static int subfacet_max_idle(const struct dpif_backer *);
static void update_stats(struct dpif_backer *);
static void rule_expire(struct rule_dpif *) OVS_REQUIRES(ofproto_mutex);
static void expire_subfacets(struct dpif_backer *, int dp_max_idle);

/* This function is called periodically by run().  Its job is to collect
 * updates for the flows that have been installed into the datapath, most
 * importantly when they last were used, and then use that information to
 * expire flows that have not been used recently.
 *
 * Returns the number of milliseconds after which it should be called again. */
static int
expire(struct dpif_backer *backer)
{
    struct ofproto_dpif *ofproto;
    size_t n_subfacets;
    int max_idle;

    /* Periodically clear out the drop keys in an effort to keep them
     * relatively few. */
    drop_key_clear(backer);

    /* Update stats for each flow in the backer. */
    update_stats(backer);

    n_subfacets = hmap_count(&backer->subfacets);
    if (n_subfacets) {
        struct subfacet *subfacet;
        long long int total, now;

        total = 0;
        now = time_msec();
        HMAP_FOR_EACH (subfacet, hmap_node, &backer->subfacets) {
            total += now - subfacet->created;
        }
        backer->avg_subfacet_life += total / n_subfacets;
    }
    backer->avg_subfacet_life /= 2;

    backer->avg_n_subfacet += n_subfacets;
    backer->avg_n_subfacet /= 2;

    backer->max_n_subfacet = MAX(backer->max_n_subfacet, n_subfacets);

    max_idle = subfacet_max_idle(backer);
    expire_subfacets(backer, max_idle);

    HMAP_FOR_EACH (ofproto, all_ofproto_dpifs_node, &all_ofproto_dpifs) {
        struct rule *rule, *next_rule;

        if (ofproto->backer != backer) {
            continue;
        }

        /* Expire OpenFlow flows whose idle_timeout or hard_timeout
         * has passed. */
        ovs_mutex_lock(&ofproto_mutex);
        LIST_FOR_EACH_SAFE (rule, next_rule, expirable,
                            &ofproto->up.expirable) {
            rule_expire(rule_dpif_cast(rule));
        }
        ovs_mutex_unlock(&ofproto_mutex);

        /* All outstanding data in existing flows has been accounted, so it's a
         * good time to do bond rebalancing. */
        if (ofproto->has_bonded_bundles) {
            struct ofbundle *bundle;

            HMAP_FOR_EACH (bundle, hmap_node, &ofproto->bundles) {
                if (bundle->bond) {
                    bond_rebalance(bundle->bond);
                }
            }
        }
    }

    return MIN(max_idle, 1000);
}

/* Updates flow table statistics given that the datapath just reported 'stats'
 * as 'subfacet''s statistics. */
static void
update_subfacet_stats(struct subfacet *subfacet,
                      const struct dpif_flow_stats *stats)
{
    struct facet *facet = subfacet->facet;
    struct dpif_flow_stats diff;

    diff.tcp_flags = stats->tcp_flags;
    diff.used = stats->used;

    if (stats->n_packets >= subfacet->dp_packet_count) {
        diff.n_packets = stats->n_packets - subfacet->dp_packet_count;
    } else {
        VLOG_WARN_RL(&rl, "unexpected packet count from the datapath");
        diff.n_packets = 0;
    }

    if (stats->n_bytes >= subfacet->dp_byte_count) {
        diff.n_bytes = stats->n_bytes - subfacet->dp_byte_count;
    } else {
        VLOG_WARN_RL(&rl, "unexpected byte count from datapath");
        diff.n_bytes = 0;
    }

    facet->ofproto->n_hit += diff.n_packets;
    subfacet->dp_packet_count = stats->n_packets;
    subfacet->dp_byte_count = stats->n_bytes;
    subfacet_update_stats(subfacet, &diff);

    if (facet->accounted_bytes < facet->byte_count) {
        facet_learn(facet);
        facet_account(facet);
        facet->accounted_bytes = facet->byte_count;
    }
}

/* 'key' with length 'key_len' bytes is a flow in 'dpif' that we know nothing
 * about, or a flow that shouldn't be installed but was anyway.  Delete it. */
static void
delete_unexpected_flow(struct dpif_backer *backer,
                       const struct nlattr *key, size_t key_len)
{
    if (!VLOG_DROP_WARN(&rl)) {
        struct ds s;

        ds_init(&s);
        odp_flow_key_format(key, key_len, &s);
        VLOG_WARN("unexpected flow: %s", ds_cstr(&s));
        ds_destroy(&s);
    }

    COVERAGE_INC(facet_unexpected);
    dpif_flow_del(backer->dpif, key, key_len, NULL);
}

/* Update 'packet_count', 'byte_count', and 'used' members of installed facets.
 *
 * This function also pushes statistics updates to rules which each facet
 * resubmits into.  Generally these statistics will be accurate.  However, if a
 * facet changes the rule it resubmits into at some time in between
 * update_stats() runs, it is possible that statistics accrued to the
 * old rule will be incorrectly attributed to the new rule.  This could be
 * avoided by calling update_stats() whenever rules are created or
 * deleted.  However, the performance impact of making so many calls to the
 * datapath do not justify the benefit of having perfectly accurate statistics.
 *
 * In addition, this function maintains per ofproto flow hit counts. The patch
 * port is not treated specially. e.g. A packet ingress from br0 patched into
 * br1 will increase the hit count of br0 by 1, however, does not affect
 * the hit or miss counts of br1.
 */
static void
update_stats(struct dpif_backer *backer)
{
    const struct dpif_flow_stats *stats;
    struct dpif_flow_dump dump;
    const struct nlattr *key, *mask;
    size_t key_len, mask_len;

    dpif_flow_dump_start(&dump, backer->dpif);
    while (dpif_flow_dump_next(&dump, &key, &key_len,
                               &mask, &mask_len, NULL, NULL, &stats)) {
        struct subfacet *subfacet;
        uint32_t key_hash;

        key_hash = odp_flow_key_hash(key, key_len);
        subfacet = subfacet_find(backer, key, key_len, key_hash);
        switch (subfacet ? subfacet->path : SF_NOT_INSTALLED) {
        case SF_FAST_PATH:
            update_subfacet_stats(subfacet, stats);
            break;

        case SF_SLOW_PATH:
            /* Stats are updated per-packet. */
            break;

        case SF_NOT_INSTALLED:
        default:
            delete_unexpected_flow(backer, key, key_len);
            break;
        }
        run_fast_rl();
    }
    dpif_flow_dump_done(&dump);

    update_moving_averages(backer);
}

/* Calculates and returns the number of milliseconds of idle time after which
 * subfacets should expire from the datapath.  When a subfacet expires, we fold
 * its statistics into its facet, and when a facet's last subfacet expires, we
 * fold its statistic into its rule. */
static int
subfacet_max_idle(const struct dpif_backer *backer)
{
    /*
     * Idle time histogram.
     *
     * Most of the time a switch has a relatively small number of subfacets.
     * When this is the case we might as well keep statistics for all of them
     * in userspace and to cache them in the kernel datapath for performance as
     * well.
     *
     * As the number of subfacets increases, the memory required to maintain
     * statistics about them in userspace and in the kernel becomes
     * significant.  However, with a large number of subfacets it is likely
     * that only a few of them are "heavy hitters" that consume a large amount
     * of bandwidth.  At this point, only heavy hitters are worth caching in
     * the kernel and maintaining in userspaces; other subfacets we can
     * discard.
     *
     * The technique used to compute the idle time is to build a histogram with
     * N_BUCKETS buckets whose width is BUCKET_WIDTH msecs each.  Each subfacet
     * that is installed in the kernel gets dropped in the appropriate bucket.
     * After the histogram has been built, we compute the cutoff so that only
     * the most-recently-used 1% of subfacets (but at least
     * flow_eviction_threshold flows) are kept cached.  At least
     * the most-recently-used bucket of subfacets is kept, so actually an
     * arbitrary number of subfacets can be kept in any given expiration run
     * (though the next run will delete most of those unless they receive
     * additional data).
     *
     * This requires a second pass through the subfacets, in addition to the
     * pass made by update_stats(), because the former function never looks at
     * uninstallable subfacets.
     */
    enum { BUCKET_WIDTH = ROUND_UP(100, TIME_UPDATE_INTERVAL) };
    enum { N_BUCKETS = 5000 / BUCKET_WIDTH };
    int buckets[N_BUCKETS] = { 0 };
    int total, subtotal, bucket;
    struct subfacet *subfacet;
    long long int now;
    int i;

    total = hmap_count(&backer->subfacets);
    if (total <= flow_eviction_threshold) {
        return N_BUCKETS * BUCKET_WIDTH;
    }

    /* Build histogram. */
    now = time_msec();
    HMAP_FOR_EACH (subfacet, hmap_node, &backer->subfacets) {
        long long int idle = now - subfacet->used;
        int bucket = (idle <= 0 ? 0
                      : idle >= BUCKET_WIDTH * N_BUCKETS ? N_BUCKETS - 1
                      : (unsigned int) idle / BUCKET_WIDTH);
        buckets[bucket]++;
    }

    /* Find the first bucket whose flows should be expired. */
    subtotal = bucket = 0;
    do {
        subtotal += buckets[bucket++];
    } while (bucket < N_BUCKETS &&
             subtotal < MAX(flow_eviction_threshold, total / 100));

    if (VLOG_IS_DBG_ENABLED()) {
        struct ds s;

        ds_init(&s);
        ds_put_cstr(&s, "keep");
        for (i = 0; i < N_BUCKETS; i++) {
            if (i == bucket) {
                ds_put_cstr(&s, ", drop");
            }
            if (buckets[i]) {
                ds_put_format(&s, " %d:%d", i * BUCKET_WIDTH, buckets[i]);
            }
        }
        VLOG_INFO("%s (msec:count)", ds_cstr(&s));
        ds_destroy(&s);
    }

    return bucket * BUCKET_WIDTH;
}

static void
expire_subfacets(struct dpif_backer *backer, int dp_max_idle)
{
    /* Cutoff time for most flows. */
    long long int normal_cutoff = time_msec() - dp_max_idle;

    /* We really want to keep flows for special protocols around, so use a more
     * conservative cutoff. */
    long long int special_cutoff = time_msec() - 10000;

    struct subfacet *subfacet, *next_subfacet;
    struct subfacet *batch[SUBFACET_DESTROY_MAX_BATCH];
    int n_batch;

    n_batch = 0;
    HMAP_FOR_EACH_SAFE (subfacet, next_subfacet, hmap_node,
                        &backer->subfacets) {
        long long int cutoff;

        cutoff = (subfacet->facet->xout.slow & (SLOW_CFM | SLOW_BFD | SLOW_LACP
                                                | SLOW_STP)
                  ? special_cutoff
                  : normal_cutoff);
        if (subfacet->used < cutoff) {
            if (subfacet->path != SF_NOT_INSTALLED) {
                batch[n_batch++] = subfacet;
                if (n_batch >= SUBFACET_DESTROY_MAX_BATCH) {
                    subfacet_destroy_batch(backer, batch, n_batch);
                    n_batch = 0;
                }
            } else {
                subfacet_destroy(subfacet);
            }
        }
    }

    if (n_batch > 0) {
        subfacet_destroy_batch(backer, batch, n_batch);
    }
}

/* If 'rule' is an OpenFlow rule, that has expired according to OpenFlow rules,
 * then delete it entirely. */
static void
rule_expire(struct rule_dpif *rule)
    OVS_REQUIRES(ofproto_mutex)
{
    uint16_t idle_timeout, hard_timeout;
    long long int now = time_msec();
    int reason;

    ovs_assert(!rule->up.pending);

    /* Has 'rule' expired? */
    ovs_mutex_lock(&rule->up.mutex);
    hard_timeout = rule->up.hard_timeout;
    idle_timeout = rule->up.idle_timeout;
    if (hard_timeout && now > rule->up.modified + hard_timeout * 1000) {
        reason = OFPRR_HARD_TIMEOUT;
    } else if (idle_timeout && now > rule->up.used + idle_timeout * 1000) {
        reason = OFPRR_IDLE_TIMEOUT;
    } else {
        reason = -1;
    }
    ovs_mutex_unlock(&rule->up.mutex);

    if (reason >= 0) {
        COVERAGE_INC(ofproto_dpif_expired);
        ofproto_rule_expire(&rule->up, reason);
    }
}

/* Facets. */

/* Creates and returns a new facet based on 'miss'.
 *
 * The caller must already have determined that no facet with an identical
 * 'miss->flow' exists in 'miss->ofproto'.
 *
 * 'rule' and 'xout' must have been created based on 'miss'.
 *
 * 'facet'' statistics are initialized based on 'stats'.
 *
 * The facet will initially have no subfacets.  The caller should create (at
 * least) one subfacet with subfacet_create(). */
static struct facet *
facet_create(const struct flow_miss *miss)
{
    struct ofproto_dpif *ofproto = miss->ofproto;
    struct facet *facet;
    struct match match;

    facet = xzalloc(sizeof *facet);
    facet->ofproto = miss->ofproto;
    facet->used = miss->stats.used;
    facet->flow = miss->flow;
    facet->learn_rl = time_msec() + 500;

    list_init(&facet->subfacets);
    netflow_flow_init(&facet->nf_flow);
    netflow_flow_update_time(ofproto->netflow, &facet->nf_flow, facet->used);

    xlate_out_copy(&facet->xout, &miss->xout);

    match_init(&match, &facet->flow, &facet->xout.wc);
    cls_rule_init(&facet->cr, &match, OFP_DEFAULT_PRIORITY);
    ovs_rwlock_wrlock(&ofproto->facets.rwlock);
    classifier_insert(&ofproto->facets, &facet->cr);
    ovs_rwlock_unlock(&ofproto->facets.rwlock);

    facet->nf_flow.output_iface = facet->xout.nf_output_iface;
    return facet;
}

static void
facet_free(struct facet *facet)
{
    if (facet) {
        xlate_out_uninit(&facet->xout);
        free(facet);
    }
}

/* Executes, within 'ofproto', the 'n_actions' actions in 'actions' on
 * 'packet', which arrived on 'in_port'. */
static bool
execute_odp_actions(struct ofproto_dpif *ofproto, const struct flow *flow,
                    const struct nlattr *odp_actions, size_t actions_len,
                    struct ofpbuf *packet)
{
    struct odputil_keybuf keybuf;
    struct ofpbuf key;
    int error;

    ofpbuf_use_stack(&key, &keybuf, sizeof keybuf);
    odp_flow_key_from_flow(&key, flow,
                           ofp_port_to_odp_port(ofproto, flow->in_port.ofp_port));

    error = dpif_execute(ofproto->backer->dpif, key.data, key.size,
                         odp_actions, actions_len, packet);
    return !error;
}

/* Remove 'facet' from its ofproto and free up the associated memory:
 *
 *   - If 'facet' was installed in the datapath, uninstalls it and updates its
 *     rule's statistics, via subfacet_uninstall().
 *
 *   - Removes 'facet' from its rule and from ofproto->facets.
 */
static void
facet_remove(struct facet *facet)
{
    struct subfacet *subfacet, *next_subfacet;

    ovs_assert(!list_is_empty(&facet->subfacets));

    /* First uninstall all of the subfacets to get final statistics. */
    LIST_FOR_EACH (subfacet, list_node, &facet->subfacets) {
        subfacet_uninstall(subfacet);
    }

    /* Flush the final stats to the rule.
     *
     * This might require us to have at least one subfacet around so that we
     * can use its actions for accounting in facet_account(), which is why we
     * have uninstalled but not yet destroyed the subfacets. */
    facet_flush_stats(facet);

    /* Now we're really all done so destroy everything. */
    LIST_FOR_EACH_SAFE (subfacet, next_subfacet, list_node,
                        &facet->subfacets) {
        subfacet_destroy__(subfacet);
    }
    ovs_rwlock_wrlock(&facet->ofproto->facets.rwlock);
    classifier_remove(&facet->ofproto->facets, &facet->cr);
    ovs_rwlock_unlock(&facet->ofproto->facets.rwlock);
    cls_rule_destroy(&facet->cr);
    facet_free(facet);
}

/* Feed information from 'facet' back into the learning table to keep it in
 * sync with what is actually flowing through the datapath. */
static void
facet_learn(struct facet *facet)
{
    long long int now = time_msec();

    if (!facet->xout.has_fin_timeout && now < facet->learn_rl) {
        return;
    }

    facet->learn_rl = now + 500;

    if (!facet->xout.has_learn
        && !facet->xout.has_normal
        && (!facet->xout.has_fin_timeout
            || !(facet->tcp_flags & (TCP_FIN | TCP_RST)))) {
        return;
    }

    facet_push_stats(facet, true);
}

static void
facet_account(struct facet *facet)
{
    const struct nlattr *a;
    unsigned int left;
    ovs_be16 vlan_tci;
    uint64_t n_bytes;

    if (!facet->xout.has_normal || !facet->ofproto->has_bonded_bundles) {
        return;
    }
    n_bytes = facet->byte_count - facet->accounted_bytes;

    /* This loop feeds byte counters to bond_account() for rebalancing to use
     * as a basis.  We also need to track the actual VLAN on which the packet
     * is going to be sent to ensure that it matches the one passed to
     * bond_choose_output_slave().  (Otherwise, we will account to the wrong
     * hash bucket.)
     *
     * We use the actions from an arbitrary subfacet because they should all
     * be equally valid for our purpose. */
    vlan_tci = facet->flow.vlan_tci;
    NL_ATTR_FOR_EACH_UNSAFE (a, left, facet->xout.odp_actions.data,
                             facet->xout.odp_actions.size) {
        const struct ovs_action_push_vlan *vlan;
        struct ofport_dpif *port;

        switch (nl_attr_type(a)) {
        case OVS_ACTION_ATTR_OUTPUT:
            port = get_odp_port(facet->ofproto, nl_attr_get_odp_port(a));
            if (port && port->bundle && port->bundle->bond) {
                bond_account(port->bundle->bond, &facet->flow,
                             vlan_tci_to_vid(vlan_tci), n_bytes);
            }
            break;

        case OVS_ACTION_ATTR_POP_VLAN:
            vlan_tci = htons(0);
            break;

        case OVS_ACTION_ATTR_PUSH_VLAN:
            vlan = nl_attr_get(a);
            vlan_tci = vlan->vlan_tci;
            break;
        }
    }
}

/* Returns true if the only action for 'facet' is to send to the controller.
 * (We don't report NetFlow expiration messages for such facets because they
 * are just part of the control logic for the network, not real traffic). */
static bool
facet_is_controller_flow(struct facet *facet)
{
    if (facet) {
        struct ofproto_dpif *ofproto = facet->ofproto;
        const struct ofpact *ofpacts;
        struct rule_actions *actions;
        struct rule_dpif *rule;
        size_t ofpacts_len;
        bool is_controller;

        rule_dpif_lookup(ofproto, &facet->flow, NULL, &rule);
        actions = rule_dpif_get_actions(rule);
        rule_dpif_unref(rule);

        ofpacts_len = actions->ofpacts_len;
        ofpacts = actions->ofpacts;
        is_controller = ofpacts_len > 0
            && ofpacts->type == OFPACT_CONTROLLER
            && ofpact_next(ofpacts) >= ofpact_end(ofpacts, ofpacts_len);
        rule_actions_unref(actions);

        return is_controller;
    }
    return false;
}

/* Folds all of 'facet''s statistics into its rule.  Also updates the
 * accounting ofhook and emits a NetFlow expiration if appropriate.  All of
 * 'facet''s statistics in the datapath should have been zeroed and folded into
 * its packet and byte counts before this function is called. */
static void
facet_flush_stats(struct facet *facet)
{
    struct ofproto_dpif *ofproto = facet->ofproto;
    struct subfacet *subfacet;

    LIST_FOR_EACH (subfacet, list_node, &facet->subfacets) {
        ovs_assert(!subfacet->dp_byte_count);
        ovs_assert(!subfacet->dp_packet_count);
    }

    facet_push_stats(facet, false);
    if (facet->accounted_bytes < facet->byte_count) {
        facet_account(facet);
        facet->accounted_bytes = facet->byte_count;
    }

    if (ofproto->netflow && !facet_is_controller_flow(facet)) {
        struct ofexpired expired;
        expired.flow = facet->flow;
        expired.packet_count = facet->packet_count;
        expired.byte_count = facet->byte_count;
        expired.used = facet->used;
        netflow_expire(ofproto->netflow, &facet->nf_flow, &expired);
    }

    /* Reset counters to prevent double counting if 'facet' ever gets
     * reinstalled. */
    facet_reset_counters(facet);

    netflow_flow_clear(&facet->nf_flow);
    facet->tcp_flags = 0;
}

/* Searches 'ofproto''s table of facets for one which would be responsible for
 * 'flow'.  Returns it if found, otherwise a null pointer.
 *
 * The returned facet might need revalidation; use facet_lookup_valid()
 * instead if that is important. */
static struct facet *
facet_find(struct ofproto_dpif *ofproto, const struct flow *flow)
{
    struct cls_rule *cr;

    ovs_rwlock_rdlock(&ofproto->facets.rwlock);
    cr = classifier_lookup(&ofproto->facets, flow, NULL);
    ovs_rwlock_unlock(&ofproto->facets.rwlock);
    return cr ? CONTAINER_OF(cr, struct facet, cr) : NULL;
}

/* Searches 'ofproto''s table of facets for one capable that covers
 * 'flow'.  Returns it if found, otherwise a null pointer.
 *
 * The returned facet is guaranteed to be valid. */
static struct facet *
facet_lookup_valid(struct ofproto_dpif *ofproto, const struct flow *flow)
{
    struct facet *facet;

    facet = facet_find(ofproto, flow);
    if (facet
        && ofproto->backer->need_revalidate
        && !facet_revalidate(facet)) {
        return NULL;
    }

    return facet;
}

static bool
facet_check_consistency(struct facet *facet)
{
    static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 15);

    struct xlate_out xout;
    struct xlate_in xin;
    bool ok;

    /* Check the datapath actions for consistency. */
    xlate_in_init(&xin, facet->ofproto, &facet->flow, NULL, 0, NULL);
    xlate_actions(&xin, &xout);

    ok = ofpbuf_equal(&facet->xout.odp_actions, &xout.odp_actions)
        && facet->xout.slow == xout.slow;
    if (!ok && !VLOG_DROP_WARN(&rl)) {
        struct ds s = DS_EMPTY_INITIALIZER;

        flow_format(&s, &facet->flow);
        ds_put_cstr(&s, ": inconsistency in facet");

        if (!ofpbuf_equal(&facet->xout.odp_actions, &xout.odp_actions)) {
            ds_put_cstr(&s, " (actions were: ");
            format_odp_actions(&s, facet->xout.odp_actions.data,
                               facet->xout.odp_actions.size);
            ds_put_cstr(&s, ") (correct actions: ");
            format_odp_actions(&s, xout.odp_actions.data,
                               xout.odp_actions.size);
            ds_put_char(&s, ')');
        }

        if (facet->xout.slow != xout.slow) {
            ds_put_format(&s, " slow path incorrect. should be %d", xout.slow);
        }

        ds_destroy(&s);
    }
    xlate_out_uninit(&xout);

    return ok;
}

/* Re-searches the classifier for 'facet':
 *
 *   - If the rule found is different from 'facet''s current rule, moves
 *     'facet' to the new rule and recompiles its actions.
 *
 *   - If the rule found is the same as 'facet''s current rule, leaves 'facet'
 *     where it is and recompiles its actions anyway.
 *
 *   - If any of 'facet''s subfacets correspond to a new flow according to
 *     xlate_receive(), 'facet' is removed.
 *
 *   Returns true if 'facet' is still valid.  False if 'facet' was removed. */
static bool
facet_revalidate(struct facet *facet)
{
    struct ofproto_dpif *ofproto = facet->ofproto;
    struct rule_dpif *new_rule;
    struct subfacet *subfacet;
    struct flow_wildcards wc;
    struct xlate_out xout;
    struct xlate_in xin;

    COVERAGE_INC(facet_revalidate);

    /* Check that child subfacets still correspond to this facet.  Tunnel
     * configuration changes could cause a subfacet's OpenFlow in_port to
     * change. */
    LIST_FOR_EACH (subfacet, list_node, &facet->subfacets) {
        struct ofproto_dpif *recv_ofproto;
        struct flow recv_flow;
        int error;

        error = xlate_receive(ofproto->backer, NULL, subfacet->key,
                              subfacet->key_len, &recv_flow, NULL,
                              &recv_ofproto, NULL);
        if (error
            || recv_ofproto != ofproto
            || facet != facet_find(ofproto, &recv_flow)) {
            facet_remove(facet);
            return false;
        }
    }

    flow_wildcards_init_catchall(&wc);
    rule_dpif_lookup(ofproto, &facet->flow, &wc, &new_rule);

    /* Calculate new datapath actions.
     *
     * We do not modify any 'facet' state yet, because we might need to, e.g.,
     * emit a NetFlow expiration and, if so, we need to have the old state
     * around to properly compose it. */
    xlate_in_init(&xin, ofproto, &facet->flow, new_rule, 0, NULL);
    xlate_actions(&xin, &xout);
    flow_wildcards_or(&xout.wc, &xout.wc, &wc);

    /* A facet's slow path reason should only change under dramatic
     * circumstances.  Rather than try to update everything, it's simpler to
     * remove the facet and start over.
     *
     * More importantly, if a facet's wildcards change, it will be relatively
     * difficult to figure out if its subfacets still belong to it, and if not
     * which facet they may belong to.  Again, to avoid the complexity, we
     * simply give up instead. */
    if (facet->xout.slow != xout.slow
        || memcmp(&facet->xout.wc, &xout.wc, sizeof xout.wc)) {
        facet_remove(facet);
        xlate_out_uninit(&xout);
        rule_dpif_unref(new_rule);
        return false;
    }

    if (!ofpbuf_equal(&facet->xout.odp_actions, &xout.odp_actions)) {
        LIST_FOR_EACH(subfacet, list_node, &facet->subfacets) {
            if (subfacet->path == SF_FAST_PATH) {
                struct dpif_flow_stats stats;

                subfacet_install(subfacet, &xout.odp_actions, &stats);
                subfacet_update_stats(subfacet, &stats);
            }
        }

        facet_flush_stats(facet);

        ofpbuf_clear(&facet->xout.odp_actions);
        ofpbuf_put(&facet->xout.odp_actions, xout.odp_actions.data,
                   xout.odp_actions.size);
    }

    /* Update 'facet' now that we've taken care of all the old state. */
    facet->xout.slow = xout.slow;
    facet->xout.has_learn = xout.has_learn;
    facet->xout.has_normal = xout.has_normal;
    facet->xout.has_fin_timeout = xout.has_fin_timeout;
    facet->xout.nf_output_iface = xout.nf_output_iface;
    facet->xout.mirrors = xout.mirrors;
    facet->nf_flow.output_iface = facet->xout.nf_output_iface;

    ovs_mutex_lock(&new_rule->up.mutex);
    facet->used = MAX(facet->used, new_rule->up.created);
    ovs_mutex_unlock(&new_rule->up.mutex);

    xlate_out_uninit(&xout);
    rule_dpif_unref(new_rule);
    return true;
}

static void
facet_reset_counters(struct facet *facet)
{
    facet->packet_count = 0;
    facet->byte_count = 0;
    facet->prev_packet_count = 0;
    facet->prev_byte_count = 0;
    facet->accounted_bytes = 0;
}

static void
flow_push_stats(struct ofproto_dpif *ofproto, struct flow *flow,
                struct dpif_flow_stats *stats, bool may_learn)
{
    struct ofport_dpif *in_port;
    struct xlate_in xin;

    in_port = get_ofp_port(ofproto, flow->in_port.ofp_port);
    if (in_port && in_port->is_tunnel) {
        netdev_vport_inc_rx(in_port->up.netdev, stats);
    }

    xlate_in_init(&xin, ofproto, flow, NULL, stats->tcp_flags, NULL);
    xin.resubmit_stats = stats;
    xin.may_learn = may_learn;
    xlate_actions_for_side_effects(&xin);
}

static void
facet_push_stats(struct facet *facet, bool may_learn)
{
    struct dpif_flow_stats stats;

    ovs_assert(facet->packet_count >= facet->prev_packet_count);
    ovs_assert(facet->byte_count >= facet->prev_byte_count);
    ovs_assert(facet->used >= facet->prev_used);

    stats.n_packets = facet->packet_count - facet->prev_packet_count;
    stats.n_bytes = facet->byte_count - facet->prev_byte_count;
    stats.used = facet->used;
    stats.tcp_flags = facet->tcp_flags;

    if (may_learn || stats.n_packets || facet->used > facet->prev_used) {
        facet->prev_packet_count = facet->packet_count;
        facet->prev_byte_count = facet->byte_count;
        facet->prev_used = facet->used;

        netflow_flow_update_time(facet->ofproto->netflow, &facet->nf_flow,
                                 facet->used);
        netflow_flow_update_flags(&facet->nf_flow, facet->tcp_flags);
        mirror_update_stats(facet->ofproto->mbridge, facet->xout.mirrors,
                            stats.n_packets, stats.n_bytes);
        flow_push_stats(facet->ofproto, &facet->flow, &stats, may_learn);
    }
}

static void
push_all_stats__(bool run_fast)
{
    static long long int rl = LLONG_MIN;
    struct ofproto_dpif *ofproto;

    if (time_msec() < rl) {
        return;
    }

    HMAP_FOR_EACH (ofproto, all_ofproto_dpifs_node, &all_ofproto_dpifs) {
        struct cls_cursor cursor;
        struct facet *facet;

        ovs_rwlock_rdlock(&ofproto->facets.rwlock);
        cls_cursor_init(&cursor, &ofproto->facets, NULL);
        CLS_CURSOR_FOR_EACH (facet, cr, &cursor) {
            facet_push_stats(facet, false);
            if (run_fast) {
                run_fast_rl();
            }
        }
        ovs_rwlock_unlock(&ofproto->facets.rwlock);
    }

    rl = time_msec() + 100;
}

static void
push_all_stats(void)
{
    push_all_stats__(true);
}

void
rule_dpif_credit_stats(struct rule_dpif *rule,
                       const struct dpif_flow_stats *stats)
{
    ovs_mutex_lock(&rule->stats_mutex);
    rule->packet_count += stats->n_packets;
    rule->byte_count += stats->n_bytes;
    rule->up.used = MAX(rule->up.used, stats->used);
    ovs_mutex_unlock(&rule->stats_mutex);
}

bool
rule_dpif_fail_open(const struct rule_dpif *rule)
{
    return rule->up.cr.priority == FAIL_OPEN_PRIORITY;
}

ovs_be64
rule_dpif_get_flow_cookie(const struct rule_dpif *rule)
    OVS_REQUIRES(rule->up.mutex)
{
    return rule->up.flow_cookie;
}

void
rule_dpif_reduce_timeouts(struct rule_dpif *rule, uint16_t idle_timeout,
                     uint16_t hard_timeout)
{
    ofproto_rule_reduce_timeouts(&rule->up, idle_timeout, hard_timeout);
}

/* Returns 'rule''s actions.  The caller owns a reference on the returned
 * actions and must eventually release it (with rule_actions_unref()) to avoid
 * a memory leak. */
struct rule_actions *
rule_dpif_get_actions(const struct rule_dpif *rule)
{
    return rule_get_actions(&rule->up);
}

/* Subfacets. */

static struct subfacet *
subfacet_find(struct dpif_backer *backer, const struct nlattr *key,
              size_t key_len, uint32_t key_hash)
{
    struct subfacet *subfacet;

    HMAP_FOR_EACH_WITH_HASH (subfacet, hmap_node, key_hash,
                             &backer->subfacets) {
        if (subfacet->key_len == key_len
            && !memcmp(key, subfacet->key, key_len)) {
            return subfacet;
        }
    }

    return NULL;
}

/* Searches 'facet' (within 'ofproto') for a subfacet with the specified
 * 'key_fitness', 'key', and 'key_len' members in 'miss'.  Returns the
 * existing subfacet if there is one, otherwise creates and returns a
 * new subfacet. */
static struct subfacet *
subfacet_create(struct facet *facet, struct flow_miss *miss)
{
    struct dpif_backer *backer = miss->ofproto->backer;
    enum odp_key_fitness key_fitness = miss->key_fitness;
    const struct nlattr *key = miss->key;
    size_t key_len = miss->key_len;
    uint32_t key_hash;
    struct subfacet *subfacet;

    key_hash = odp_flow_key_hash(key, key_len);

    if (list_is_empty(&facet->subfacets)) {
        subfacet = &facet->one_subfacet;
    } else {
        subfacet = subfacet_find(backer, key, key_len, key_hash);
        if (subfacet) {
            if (subfacet->facet == facet) {
                return subfacet;
            }

            /* This shouldn't happen. */
            VLOG_ERR_RL(&rl, "subfacet with wrong facet");
            subfacet_destroy(subfacet);
        }

        subfacet = xmalloc(sizeof *subfacet);
    }

    hmap_insert(&backer->subfacets, &subfacet->hmap_node, key_hash);
    list_push_back(&facet->subfacets, &subfacet->list_node);
    subfacet->facet = facet;
    subfacet->key_fitness = key_fitness;
    subfacet->key = xmemdup(key, key_len);
    subfacet->key_len = key_len;
    subfacet->used = miss->stats.used;
    subfacet->created = subfacet->used;
    subfacet->dp_packet_count = 0;
    subfacet->dp_byte_count = 0;
    subfacet->path = SF_NOT_INSTALLED;
    subfacet->backer = backer;

    backer->subfacet_add_count++;
    return subfacet;
}

/* Uninstalls 'subfacet' from the datapath, if it is installed, removes it from
 * its facet within 'ofproto', and frees it. */
static void
subfacet_destroy__(struct subfacet *subfacet)
{
    struct facet *facet = subfacet->facet;
    struct ofproto_dpif *ofproto = facet->ofproto;

    /* Update ofproto stats before uninstall the subfacet. */
    ofproto->backer->subfacet_del_count++;

    subfacet_uninstall(subfacet);
    hmap_remove(&subfacet->backer->subfacets, &subfacet->hmap_node);
    list_remove(&subfacet->list_node);
    free(subfacet->key);
    if (subfacet != &facet->one_subfacet) {
        free(subfacet);
    }
}

/* Destroys 'subfacet', as with subfacet_destroy__(), and then if this was the
 * last remaining subfacet in its facet destroys the facet too. */
static void
subfacet_destroy(struct subfacet *subfacet)
{
    struct facet *facet = subfacet->facet;

    if (list_is_singleton(&facet->subfacets)) {
        /* facet_remove() needs at least one subfacet (it will remove it). */
        facet_remove(facet);
    } else {
        subfacet_destroy__(subfacet);
    }
}

static void
subfacet_destroy_batch(struct dpif_backer *backer,
                       struct subfacet **subfacets, int n)
{
    struct dpif_op ops[SUBFACET_DESTROY_MAX_BATCH];
    struct dpif_op *opsp[SUBFACET_DESTROY_MAX_BATCH];
    struct dpif_flow_stats stats[SUBFACET_DESTROY_MAX_BATCH];
    int i;

    for (i = 0; i < n; i++) {
        ops[i].type = DPIF_OP_FLOW_DEL;
        ops[i].u.flow_del.key = subfacets[i]->key;
        ops[i].u.flow_del.key_len = subfacets[i]->key_len;
        ops[i].u.flow_del.stats = &stats[i];
        opsp[i] = &ops[i];
    }

    dpif_operate(backer->dpif, opsp, n);
    for (i = 0; i < n; i++) {
        subfacet_reset_dp_stats(subfacets[i], &stats[i]);
        subfacets[i]->path = SF_NOT_INSTALLED;
        subfacet_destroy(subfacets[i]);
        run_fast_rl();
    }
}

/* Updates 'subfacet''s datapath flow, setting its actions to 'actions_len'
 * bytes of actions in 'actions'.  If 'stats' is non-null, statistics counters
 * in the datapath will be zeroed and 'stats' will be updated with traffic new
 * since 'subfacet' was last updated.
 *
 * Returns 0 if successful, otherwise a positive errno value. */
static int
subfacet_install(struct subfacet *subfacet, const struct ofpbuf *odp_actions,
                 struct dpif_flow_stats *stats)
{
    struct facet *facet = subfacet->facet;
    enum subfacet_path path = facet->xout.slow ? SF_SLOW_PATH : SF_FAST_PATH;
    const struct nlattr *actions = odp_actions->data;
    size_t actions_len = odp_actions->size;
    struct odputil_keybuf maskbuf;
    struct ofpbuf mask;

    uint64_t slow_path_stub[128 / 8];
    enum dpif_flow_put_flags flags;
    int ret;

    flags = subfacet->path == SF_NOT_INSTALLED ? DPIF_FP_CREATE
                                               : DPIF_FP_MODIFY;
    if (stats) {
        flags |= DPIF_FP_ZERO_STATS;
    }

    if (path == SF_SLOW_PATH) {
        compose_slow_path(facet->ofproto, &facet->flow, facet->xout.slow,
                          slow_path_stub, sizeof slow_path_stub,
                          &actions, &actions_len);
    }

    ofpbuf_use_stack(&mask, &maskbuf, sizeof maskbuf);
    if (enable_megaflows) {
        odp_flow_key_from_mask(&mask, &facet->xout.wc.masks,
                               &facet->flow, UINT32_MAX);
    }

    ret = dpif_flow_put(subfacet->backer->dpif, flags, subfacet->key,
                        subfacet->key_len,  mask.data, mask.size,
                        actions, actions_len, stats);

    if (stats) {
        subfacet_reset_dp_stats(subfacet, stats);
    }

    if (ret) {
        COVERAGE_INC(subfacet_install_fail);
    } else {
        subfacet->path = path;
    }
    return ret;
}

/* If 'subfacet' is installed in the datapath, uninstalls it. */
static void
subfacet_uninstall(struct subfacet *subfacet)
{
    if (subfacet->path != SF_NOT_INSTALLED) {
        struct ofproto_dpif *ofproto = subfacet->facet->ofproto;
        struct dpif_flow_stats stats;
        int error;

        error = dpif_flow_del(ofproto->backer->dpif, subfacet->key,
                              subfacet->key_len, &stats);
        subfacet_reset_dp_stats(subfacet, &stats);
        if (!error) {
            subfacet_update_stats(subfacet, &stats);
        }
        subfacet->path = SF_NOT_INSTALLED;
    } else {
        ovs_assert(subfacet->dp_packet_count == 0);
        ovs_assert(subfacet->dp_byte_count == 0);
    }
}

/* Resets 'subfacet''s datapath statistics counters.  This should be called
 * when 'subfacet''s statistics are cleared in the datapath.  If 'stats' is
 * non-null, it should contain the statistics returned by dpif when 'subfacet'
 * was reset in the datapath.  'stats' will be modified to include only
 * statistics new since 'subfacet' was last updated. */
static void
subfacet_reset_dp_stats(struct subfacet *subfacet,
                        struct dpif_flow_stats *stats)
{
    if (stats
        && subfacet->dp_packet_count <= stats->n_packets
        && subfacet->dp_byte_count <= stats->n_bytes) {
        stats->n_packets -= subfacet->dp_packet_count;
        stats->n_bytes -= subfacet->dp_byte_count;
    }

    subfacet->dp_packet_count = 0;
    subfacet->dp_byte_count = 0;
}

/* Folds the statistics from 'stats' into the counters in 'subfacet'.
 *
 * Because of the meaning of a subfacet's counters, it only makes sense to do
 * this if 'stats' are not tracked in the datapath, that is, if 'stats'
 * represents a packet that was sent by hand or if it represents statistics
 * that have been cleared out of the datapath. */
static void
subfacet_update_stats(struct subfacet *subfacet,
                      const struct dpif_flow_stats *stats)
{
    if (stats->n_packets || stats->used > subfacet->used) {
        struct facet *facet = subfacet->facet;

        subfacet->used = MAX(subfacet->used, stats->used);
        facet->used = MAX(facet->used, stats->used);
        facet->packet_count += stats->n_packets;
        facet->byte_count += stats->n_bytes;
        facet->tcp_flags |= stats->tcp_flags;
    }
}

/* Rules. */

/* Lookup 'flow' in 'ofproto''s classifier.  If 'wc' is non-null, sets
 * the fields that were relevant as part of the lookup. */
void
rule_dpif_lookup(struct ofproto_dpif *ofproto, const struct flow *flow,
                 struct flow_wildcards *wc, struct rule_dpif **rule)
{
    struct ofport_dpif *port;

    if (rule_dpif_lookup_in_table(ofproto, flow, wc, 0, rule)) {
        return;
    }
    port = get_ofp_port(ofproto, flow->in_port.ofp_port);
    if (!port) {
        VLOG_WARN_RL(&rl, "packet-in on unknown OpenFlow port %"PRIu16,
                     flow->in_port.ofp_port);
    }

    choose_miss_rule(port ? port->up.pp.config : 0, ofproto->miss_rule,
                     ofproto->no_packet_in_rule, rule);
}

bool
rule_dpif_lookup_in_table(struct ofproto_dpif *ofproto,
                          const struct flow *flow, struct flow_wildcards *wc,
                          uint8_t table_id, struct rule_dpif **rule)
{
    const struct cls_rule *cls_rule;
    struct classifier *cls;
    bool frag;

    *rule = NULL;
    if (table_id >= N_TABLES) {
        return false;
    }

    if (wc) {
        memset(&wc->masks.dl_type, 0xff, sizeof wc->masks.dl_type);
        wc->masks.nw_frag |= FLOW_NW_FRAG_MASK;
    }

    cls = &ofproto->up.tables[table_id].cls;
    ovs_rwlock_rdlock(&cls->rwlock);
    frag = (flow->nw_frag & FLOW_NW_FRAG_ANY) != 0;
    if (frag && ofproto->up.frag_handling == OFPC_FRAG_NORMAL) {
        /* We must pretend that transport ports are unavailable. */
        struct flow ofpc_normal_flow = *flow;
        ofpc_normal_flow.tp_src = htons(0);
        ofpc_normal_flow.tp_dst = htons(0);
        cls_rule = classifier_lookup(cls, &ofpc_normal_flow, wc);
    } else if (frag && ofproto->up.frag_handling == OFPC_FRAG_DROP) {
        cls_rule = &ofproto->drop_frags_rule->up.cr;
        if (wc) {
            flow_wildcards_init_exact(wc);
        }
    } else {
        cls_rule = classifier_lookup(cls, flow, wc);
    }

    *rule = rule_dpif_cast(rule_from_cls_rule(cls_rule));
    rule_dpif_ref(*rule);
    ovs_rwlock_unlock(&cls->rwlock);

    return *rule != NULL;
}

/* Given a port configuration (specified as zero if there's no port), chooses
 * which of 'miss_rule' and 'no_packet_in_rule' should be used in case of a
 * flow table miss. */
void
choose_miss_rule(enum ofputil_port_config config, struct rule_dpif *miss_rule,
                 struct rule_dpif *no_packet_in_rule, struct rule_dpif **rule)
{
    *rule = config & OFPUTIL_PC_NO_PACKET_IN ? no_packet_in_rule : miss_rule;
    rule_dpif_ref(*rule);
}

void
rule_dpif_ref(struct rule_dpif *rule)
{
    if (rule) {
        ofproto_rule_ref(&rule->up);
    }
}

void
rule_dpif_unref(struct rule_dpif *rule)
{
    if (rule) {
        ofproto_rule_unref(&rule->up);
    }
}

static void
complete_operation(struct rule_dpif *rule)
    OVS_REQUIRES(ofproto_mutex)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(rule->up.ofproto);

    ofproto->backer->need_revalidate = REV_FLOW_TABLE;
    ofoperation_complete(rule->up.pending, 0);
}

static struct rule_dpif *rule_dpif_cast(const struct rule *rule)
{
    return rule ? CONTAINER_OF(rule, struct rule_dpif, up) : NULL;
}

static struct rule *
rule_alloc(void)
{
    struct rule_dpif *rule = xmalloc(sizeof *rule);
    return &rule->up;
}

static void
rule_dealloc(struct rule *rule_)
{
    struct rule_dpif *rule = rule_dpif_cast(rule_);
    free(rule);
}

static enum ofperr
rule_construct(struct rule *rule_)
{
    struct rule_dpif *rule = rule_dpif_cast(rule_);
    ovs_mutex_init(&rule->stats_mutex);
    ovs_mutex_lock(&rule->stats_mutex);
    rule->packet_count = 0;
    rule->byte_count = 0;
    ovs_mutex_unlock(&rule->stats_mutex);
    return 0;
}

static void
rule_insert(struct rule *rule_)
    OVS_REQUIRES(ofproto_mutex)
{
    struct rule_dpif *rule = rule_dpif_cast(rule_);
    complete_operation(rule);
}

static void
rule_delete(struct rule *rule_)
    OVS_REQUIRES(ofproto_mutex)
{
    struct rule_dpif *rule = rule_dpif_cast(rule_);
    complete_operation(rule);
}

static void
rule_destruct(struct rule *rule_)
{
    struct rule_dpif *rule = rule_dpif_cast(rule_);
    ovs_mutex_destroy(&rule->stats_mutex);
}

static void
rule_get_stats(struct rule *rule_, uint64_t *packets, uint64_t *bytes)
{
    struct rule_dpif *rule = rule_dpif_cast(rule_);

    /* push_all_stats() can handle flow misses which, when using the learn
     * action, can cause rules to be added and deleted.  This can corrupt our
     * caller's datastructures which assume that rule_get_stats() doesn't have
     * an impact on the flow table. To be safe, we disable miss handling. */
    push_all_stats__(false);

    /* Start from historical data for 'rule' itself that are no longer tracked
     * in facets.  This counts, for example, facets that have expired. */
    ovs_mutex_lock(&rule->stats_mutex);
    *packets = rule->packet_count;
    *bytes = rule->byte_count;
    ovs_mutex_unlock(&rule->stats_mutex);
}

static void
rule_dpif_execute(struct rule_dpif *rule, const struct flow *flow,
                  struct ofpbuf *packet)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(rule->up.ofproto);
    struct dpif_flow_stats stats;
    struct xlate_out xout;
    struct xlate_in xin;

    dpif_flow_stats_extract(flow, packet, time_msec(), &stats);
    rule_dpif_credit_stats(rule, &stats);

    xlate_in_init(&xin, ofproto, flow, rule, stats.tcp_flags, packet);
    xin.resubmit_stats = &stats;
    xlate_actions(&xin, &xout);

    execute_odp_actions(ofproto, flow, xout.odp_actions.data,
                        xout.odp_actions.size, packet);

    xlate_out_uninit(&xout);
}

static enum ofperr
rule_execute(struct rule *rule, const struct flow *flow,
             struct ofpbuf *packet)
{
    rule_dpif_execute(rule_dpif_cast(rule), flow, packet);
    ofpbuf_delete(packet);
    return 0;
}

static void
rule_modify_actions(struct rule *rule_, bool reset_counters)
    OVS_REQUIRES(ofproto_mutex)
{
    struct rule_dpif *rule = rule_dpif_cast(rule_);

    if (reset_counters) {
        ovs_mutex_lock(&rule->stats_mutex);
        rule->packet_count = 0;
        rule->byte_count = 0;
        ovs_mutex_unlock(&rule->stats_mutex);
    }

    complete_operation(rule);
}

/* Sends 'packet' out 'ofport'.
 * May modify 'packet'.
 * Returns 0 if successful, otherwise a positive errno value. */
static int
send_packet(const struct ofport_dpif *ofport, struct ofpbuf *packet)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofport->up.ofproto);
    uint64_t odp_actions_stub[1024 / 8];
    struct ofpbuf key, odp_actions;
    struct dpif_flow_stats stats;
    struct odputil_keybuf keybuf;
    struct ofpact_output output;
    struct xlate_out xout;
    struct xlate_in xin;
    struct flow flow;
    union flow_in_port in_port_;
    int error;

    ofpbuf_use_stub(&odp_actions, odp_actions_stub, sizeof odp_actions_stub);
    ofpbuf_use_stack(&key, &keybuf, sizeof keybuf);

    /* Use OFPP_NONE as the in_port to avoid special packet processing. */
    in_port_.ofp_port = OFPP_NONE;
    flow_extract(packet, 0, 0, NULL, &in_port_, &flow);
    odp_flow_key_from_flow(&key, &flow, ofp_port_to_odp_port(ofproto,
                                                             OFPP_LOCAL));
    dpif_flow_stats_extract(&flow, packet, time_msec(), &stats);

    ofpact_init(&output.ofpact, OFPACT_OUTPUT, sizeof output);
    output.port = ofport->up.ofp_port;
    output.max_len = 0;

    xlate_in_init(&xin, ofproto, &flow, NULL, 0, packet);
    xin.ofpacts_len = sizeof output;
    xin.ofpacts = &output.ofpact;
    xin.resubmit_stats = &stats;
    xlate_actions(&xin, &xout);

    error = dpif_execute(ofproto->backer->dpif,
                         key.data, key.size,
                         xout.odp_actions.data, xout.odp_actions.size,
                         packet);
    xlate_out_uninit(&xout);

    if (error) {
        VLOG_WARN_RL(&rl, "%s: failed to send packet on port %s (%s)",
                     ofproto->up.name, netdev_get_name(ofport->up.netdev),
                     ovs_strerror(error));
    }

    ofproto->stats.tx_packets++;
    ofproto->stats.tx_bytes += packet->size;
    return error;
}

/* Composes an ODP action for a "slow path" action for 'flow' within 'ofproto'.
 * The action will state 'slow' as the reason that the action is in the slow
 * path.  (This is purely informational: it allows a human viewing "ovs-dpctl
 * dump-flows" output to see why a flow is in the slow path.)
 *
 * The 'stub_size' bytes in 'stub' will be used to store the action.
 * 'stub_size' must be large enough for the action.
 *
 * The action and its size will be stored in '*actionsp' and '*actions_lenp',
 * respectively. */
static void
compose_slow_path(const struct ofproto_dpif *ofproto, const struct flow *flow,
                  enum slow_path_reason slow,
                  uint64_t *stub, size_t stub_size,
                  const struct nlattr **actionsp, size_t *actions_lenp)
{
    union user_action_cookie cookie;
    struct ofpbuf buf;

    cookie.type = USER_ACTION_COOKIE_SLOW_PATH;
    cookie.slow_path.unused = 0;
    cookie.slow_path.reason = slow;

    ofpbuf_use_stack(&buf, stub, stub_size);
    if (slow & (SLOW_CFM | SLOW_BFD | SLOW_LACP | SLOW_STP)) {
        uint32_t pid = dpif_port_get_pid(ofproto->backer->dpif,
                                         ODPP_NONE);
        odp_put_userspace_action(pid, &cookie, sizeof cookie.slow_path, &buf);
    } else {
        odp_port_t odp_port;
        uint32_t pid;

        odp_port = ofp_port_to_odp_port(ofproto, flow->in_port.ofp_port);
        pid = dpif_port_get_pid(ofproto->backer->dpif, odp_port);
        odp_put_userspace_action(pid, &cookie, sizeof cookie.slow_path, &buf);
    }
    *actionsp = buf.data;
    *actions_lenp = buf.size;
}

static bool
set_frag_handling(struct ofproto *ofproto_,
                  enum ofp_config_flags frag_handling)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    if (frag_handling != OFPC_FRAG_REASM) {
        ofproto->backer->need_revalidate = REV_RECONFIGURE;
        return true;
    } else {
        return false;
    }
}

static enum ofperr
packet_out(struct ofproto *ofproto_, struct ofpbuf *packet,
           const struct flow *flow,
           const struct ofpact *ofpacts, size_t ofpacts_len)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);
    struct odputil_keybuf keybuf;
    struct dpif_flow_stats stats;
    struct xlate_out xout;
    struct xlate_in xin;
    struct ofpbuf key;


    ofpbuf_use_stack(&key, &keybuf, sizeof keybuf);
    odp_flow_key_from_flow(&key, flow,
                           ofp_port_to_odp_port(ofproto,
                                      flow->in_port.ofp_port));

    dpif_flow_stats_extract(flow, packet, time_msec(), &stats);

    xlate_in_init(&xin, ofproto, flow, NULL, stats.tcp_flags, packet);
    xin.resubmit_stats = &stats;
    xin.ofpacts_len = ofpacts_len;
    xin.ofpacts = ofpacts;

    xlate_actions(&xin, &xout);
    dpif_execute(ofproto->backer->dpif, key.data, key.size,
                 xout.odp_actions.data, xout.odp_actions.size, packet);
    xlate_out_uninit(&xout);

    return 0;
}

/* NetFlow. */

static int
set_netflow(struct ofproto *ofproto_,
            const struct netflow_options *netflow_options)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);

    if (netflow_options) {
        if (!ofproto->netflow) {
            ofproto->netflow = netflow_create();
            ofproto->backer->need_revalidate = REV_RECONFIGURE;
        }
        return netflow_set_options(ofproto->netflow, netflow_options);
    } else if (ofproto->netflow) {
        ofproto->backer->need_revalidate = REV_RECONFIGURE;
        netflow_destroy(ofproto->netflow);
        ofproto->netflow = NULL;
    }

    return 0;
}

static void
get_netflow_ids(const struct ofproto *ofproto_,
                uint8_t *engine_type, uint8_t *engine_id)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofproto_);

    dpif_get_netflow_ids(ofproto->backer->dpif, engine_type, engine_id);
}

static void
send_active_timeout(struct ofproto_dpif *ofproto, struct facet *facet)
{
    if (!facet_is_controller_flow(facet) &&
        netflow_active_timeout_expired(ofproto->netflow, &facet->nf_flow)) {
        struct subfacet *subfacet;
        struct ofexpired expired;

        LIST_FOR_EACH (subfacet, list_node, &facet->subfacets) {
            if (subfacet->path == SF_FAST_PATH) {
                struct dpif_flow_stats stats;

                subfacet_install(subfacet, &facet->xout.odp_actions,
                                 &stats);
                subfacet_update_stats(subfacet, &stats);
            }
        }

        expired.flow = facet->flow;
        expired.packet_count = facet->packet_count;
        expired.byte_count = facet->byte_count;
        expired.used = facet->used;
        netflow_expire(ofproto->netflow, &facet->nf_flow, &expired);
    }
}

static void
send_netflow_active_timeouts(struct ofproto_dpif *ofproto)
{
    struct cls_cursor cursor;
    struct facet *facet;

    ovs_rwlock_rdlock(&ofproto->facets.rwlock);
    cls_cursor_init(&cursor, &ofproto->facets, NULL);
    CLS_CURSOR_FOR_EACH (facet, cr, &cursor) {
        send_active_timeout(ofproto, facet);
    }
    ovs_rwlock_unlock(&ofproto->facets.rwlock);
}

static struct ofproto_dpif *
ofproto_dpif_lookup(const char *name)
{
    struct ofproto_dpif *ofproto;

    HMAP_FOR_EACH_WITH_HASH (ofproto, all_ofproto_dpifs_node,
                             hash_string(name, 0), &all_ofproto_dpifs) {
        if (!strcmp(ofproto->up.name, name)) {
            return ofproto;
        }
    }
    return NULL;
}

static void
ofproto_unixctl_fdb_flush(struct unixctl_conn *conn, int argc,
                          const char *argv[], void *aux OVS_UNUSED)
{
    struct ofproto_dpif *ofproto;

    if (argc > 1) {
        ofproto = ofproto_dpif_lookup(argv[1]);
        if (!ofproto) {
            unixctl_command_reply_error(conn, "no such bridge");
            return;
        }
        ovs_rwlock_wrlock(&ofproto->ml->rwlock);
        mac_learning_flush(ofproto->ml);
        ovs_rwlock_unlock(&ofproto->ml->rwlock);
    } else {
        HMAP_FOR_EACH (ofproto, all_ofproto_dpifs_node, &all_ofproto_dpifs) {
            ovs_rwlock_wrlock(&ofproto->ml->rwlock);
            mac_learning_flush(ofproto->ml);
            ovs_rwlock_unlock(&ofproto->ml->rwlock);
        }
    }

    unixctl_command_reply(conn, "table successfully flushed");
}

static struct ofport_dpif *
ofbundle_get_a_port(const struct ofbundle *bundle)
{
    return CONTAINER_OF(list_front(&bundle->ports), struct ofport_dpif,
                        bundle_node);
}

static void
ofproto_unixctl_fdb_show(struct unixctl_conn *conn, int argc OVS_UNUSED,
                         const char *argv[], void *aux OVS_UNUSED)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    const struct ofproto_dpif *ofproto;
    const struct mac_entry *e;

    ofproto = ofproto_dpif_lookup(argv[1]);
    if (!ofproto) {
        unixctl_command_reply_error(conn, "no such bridge");
        return;
    }

    ds_put_cstr(&ds, " port  VLAN  MAC                Age\n");
    ovs_rwlock_rdlock(&ofproto->ml->rwlock);
    LIST_FOR_EACH (e, lru_node, &ofproto->ml->lrus) {
        struct ofbundle *bundle = e->port.p;
        char name[OFP_MAX_PORT_NAME_LEN];

        ofputil_port_to_string(ofbundle_get_a_port(bundle)->up.ofp_port,
                               name, sizeof name);
        ds_put_format(&ds, "%5s  %4d  "ETH_ADDR_FMT"  %3d\n",
                      name, e->vlan, ETH_ADDR_ARGS(e->mac),
                      mac_entry_age(ofproto->ml, e));
    }
    ovs_rwlock_unlock(&ofproto->ml->rwlock);
    unixctl_command_reply(conn, ds_cstr(&ds));
    ds_destroy(&ds);
}

struct trace_ctx {
    struct xlate_out xout;
    struct xlate_in xin;
    struct flow flow;
    struct ds *result;
};

static void
trace_format_rule(struct ds *result, int level, const struct rule_dpif *rule)
{
    struct rule_actions *actions;
    ovs_be64 cookie;

    ds_put_char_multiple(result, '\t', level);
    if (!rule) {
        ds_put_cstr(result, "No match\n");
        return;
    }

    ovs_mutex_lock(&rule->up.mutex);
    cookie = rule->up.flow_cookie;
    ovs_mutex_unlock(&rule->up.mutex);

    ds_put_format(result, "Rule: table=%"PRIu8" cookie=%#"PRIx64" ",
                  rule ? rule->up.table_id : 0, ntohll(cookie));
    cls_rule_format(&rule->up.cr, result);
    ds_put_char(result, '\n');

    actions = rule_dpif_get_actions(rule);

    ds_put_char_multiple(result, '\t', level);
    ds_put_cstr(result, "OpenFlow ");
    ofpacts_format(actions->ofpacts, actions->ofpacts_len, result);
    ds_put_char(result, '\n');

    rule_actions_unref(actions);
}

static void
trace_format_flow(struct ds *result, int level, const char *title,
                  struct trace_ctx *trace)
{
    ds_put_char_multiple(result, '\t', level);
    ds_put_format(result, "%s: ", title);
    if (flow_equal(&trace->xin.flow, &trace->flow)) {
        ds_put_cstr(result, "unchanged");
    } else {
        flow_format(result, &trace->xin.flow);
        trace->flow = trace->xin.flow;
    }
    ds_put_char(result, '\n');
}

static void
trace_format_regs(struct ds *result, int level, const char *title,
                  struct trace_ctx *trace)
{
    size_t i;

    ds_put_char_multiple(result, '\t', level);
    ds_put_format(result, "%s:", title);
    for (i = 0; i < FLOW_N_REGS; i++) {
        ds_put_format(result, " reg%zu=0x%"PRIx32, i, trace->flow.regs[i]);
    }
    ds_put_char(result, '\n');
}

static void
trace_format_odp(struct ds *result, int level, const char *title,
                 struct trace_ctx *trace)
{
    struct ofpbuf *odp_actions = &trace->xout.odp_actions;

    ds_put_char_multiple(result, '\t', level);
    ds_put_format(result, "%s: ", title);
    format_odp_actions(result, odp_actions->data, odp_actions->size);
    ds_put_char(result, '\n');
}

static void
trace_resubmit(struct xlate_in *xin, struct rule_dpif *rule, int recurse)
{
    struct trace_ctx *trace = CONTAINER_OF(xin, struct trace_ctx, xin);
    struct ds *result = trace->result;

    ds_put_char(result, '\n');
    trace_format_flow(result, recurse + 1, "Resubmitted flow", trace);
    trace_format_regs(result, recurse + 1, "Resubmitted regs", trace);
    trace_format_odp(result,  recurse + 1, "Resubmitted  odp", trace);
    trace_format_rule(result, recurse + 1, rule);
}

static void
trace_report(struct xlate_in *xin, const char *s, int recurse)
{
    struct trace_ctx *trace = CONTAINER_OF(xin, struct trace_ctx, xin);
    struct ds *result = trace->result;

    ds_put_char_multiple(result, '\t', recurse);
    ds_put_cstr(result, s);
    ds_put_char(result, '\n');
}

static void
ofproto_unixctl_trace(struct unixctl_conn *conn, int argc, const char *argv[],
                      void *aux OVS_UNUSED)
{
    const struct dpif_backer *backer;
    struct ofproto_dpif *ofproto;
    struct ofpbuf odp_key, odp_mask;
    struct ofpbuf *packet;
    struct ds result;
    struct flow flow;
    char *s;

    packet = NULL;
    backer = NULL;
    ds_init(&result);
    ofpbuf_init(&odp_key, 0);
    ofpbuf_init(&odp_mask, 0);

    /* Handle "-generate" or a hex string as the last argument. */
    if (!strcmp(argv[argc - 1], "-generate")) {
        packet = ofpbuf_new(0);
        argc--;
    } else {
        const char *error = eth_from_hex(argv[argc - 1], &packet);
        if (!error) {
            argc--;
        } else if (argc == 4) {
            /* The 3-argument form must end in "-generate' or a hex string. */
            unixctl_command_reply_error(conn, error);
            goto exit;
        }
    }

    /* Parse the flow and determine whether a datapath or
     * bridge is specified. If function odp_flow_key_from_string()
     * returns 0, the flow is a odp_flow. If function
     * parse_ofp_exact_flow() returns 0, the flow is a br_flow. */
    if (!odp_flow_from_string(argv[argc - 1], NULL, &odp_key, &odp_mask)) {
        /* If the odp_flow is the second argument,
         * the datapath name is the first argument. */
        if (argc == 3) {
            const char *dp_type;
            if (!strncmp(argv[1], "ovs-", 4)) {
                dp_type = argv[1] + 4;
            } else {
                dp_type = argv[1];
            }
            backer = shash_find_data(&all_dpif_backers, dp_type);
            if (!backer) {
                unixctl_command_reply_error(conn, "Cannot find datapath "
                               "of this name");
                goto exit;
            }
        } else {
            /* No datapath name specified, so there should be only one
             * datapath. */
            struct shash_node *node;
            if (shash_count(&all_dpif_backers) != 1) {
                unixctl_command_reply_error(conn, "Must specify datapath "
                         "name, there is more than one type of datapath");
                goto exit;
            }
            node = shash_first(&all_dpif_backers);
            backer = node->data;
        }

        if (xlate_receive(backer, NULL, odp_key.data, odp_key.size, &flow,
                          NULL, &ofproto, NULL)) {
            unixctl_command_reply_error(conn, "Invalid datapath flow");
            goto exit;
        }
        ds_put_format(&result, "Bridge: %s\n", ofproto->up.name);
    } else if (!parse_ofp_exact_flow(&flow, argv[argc - 1])) {
        if (argc != 3) {
            unixctl_command_reply_error(conn, "Must specify bridge name");
            goto exit;
        }

        ofproto = ofproto_dpif_lookup(argv[1]);
        if (!ofproto) {
            unixctl_command_reply_error(conn, "Unknown bridge name");
            goto exit;
        }
    } else {
        unixctl_command_reply_error(conn, "Bad flow syntax");
        goto exit;
    }

    /* Generate a packet, if requested. */
    if (packet) {
        if (!packet->size) {
            flow_compose(packet, &flow);
        } else {
            union flow_in_port in_port_;

            in_port_ = flow.in_port;
            ds_put_cstr(&result, "Packet: ");
            s = ofp_packet_to_string(packet->data, packet->size);
            ds_put_cstr(&result, s);
            free(s);

            /* Use the metadata from the flow and the packet argument
             * to reconstruct the flow. */
            flow_extract(packet, flow.skb_priority, flow.pkt_mark, NULL,
                         &in_port_, &flow);
        }
    }

    ofproto_trace(ofproto, &flow, packet, &result);
    unixctl_command_reply(conn, ds_cstr(&result));

exit:
    ds_destroy(&result);
    ofpbuf_delete(packet);
    ofpbuf_uninit(&odp_key);
    ofpbuf_uninit(&odp_mask);
}

static void
ofproto_trace(struct ofproto_dpif *ofproto, const struct flow *flow,
              const struct ofpbuf *packet, struct ds *ds)
{
    struct rule_dpif *rule;
    struct flow_wildcards wc;

    ds_put_cstr(ds, "Flow: ");
    flow_format(ds, flow);
    ds_put_char(ds, '\n');

    flow_wildcards_init_catchall(&wc);
    rule_dpif_lookup(ofproto, flow, &wc, &rule);

    trace_format_rule(ds, 0, rule);
    if (rule == ofproto->miss_rule) {
        ds_put_cstr(ds, "\nNo match, flow generates \"packet in\"s.\n");
    } else if (rule == ofproto->no_packet_in_rule) {
        ds_put_cstr(ds, "\nNo match, packets dropped because "
                    "OFPPC_NO_PACKET_IN is set on in_port.\n");
    } else if (rule == ofproto->drop_frags_rule) {
        ds_put_cstr(ds, "\nPackets dropped because they are IP fragments "
                    "and the fragment handling mode is \"drop\".\n");
    }

    if (rule) {
        uint64_t odp_actions_stub[1024 / 8];
        struct ofpbuf odp_actions;
        struct trace_ctx trace;
        struct match match;
        uint8_t tcp_flags;

        tcp_flags = packet ? packet_get_tcp_flags(packet, flow) : 0;
        trace.result = ds;
        trace.flow = *flow;
        ofpbuf_use_stub(&odp_actions,
                        odp_actions_stub, sizeof odp_actions_stub);
        xlate_in_init(&trace.xin, ofproto, flow, rule, tcp_flags, packet);
        trace.xin.resubmit_hook = trace_resubmit;
        trace.xin.report_hook = trace_report;

        xlate_actions(&trace.xin, &trace.xout);
        flow_wildcards_or(&trace.xout.wc, &trace.xout.wc, &wc);

        ds_put_char(ds, '\n');
        trace_format_flow(ds, 0, "Final flow", &trace);

        match_init(&match, flow, &trace.xout.wc);
        ds_put_cstr(ds, "Relevant fields: ");
        match_format(&match, ds, OFP_DEFAULT_PRIORITY);
        ds_put_char(ds, '\n');

        ds_put_cstr(ds, "Datapath actions: ");
        format_odp_actions(ds, trace.xout.odp_actions.data,
                           trace.xout.odp_actions.size);

        if (trace.xout.slow) {
            ds_put_cstr(ds, "\nThis flow is handled by the userspace "
                        "slow path because it:");
            switch (trace.xout.slow) {
            case SLOW_CFM:
                ds_put_cstr(ds, "\n\t- Consists of CFM packets.");
                break;
            case SLOW_LACP:
                ds_put_cstr(ds, "\n\t- Consists of LACP packets.");
                break;
            case SLOW_STP:
                ds_put_cstr(ds, "\n\t- Consists of STP packets.");
                break;
            case SLOW_BFD:
                ds_put_cstr(ds, "\n\t- Consists of BFD packets.");
                break;
            case SLOW_CONTROLLER:
                ds_put_cstr(ds, "\n\t- Sends \"packet-in\" messages "
                            "to the OpenFlow controller.");
                break;
            case __SLOW_MAX:
                NOT_REACHED();
            }
        }

        xlate_out_uninit(&trace.xout);
    }

    rule_dpif_unref(rule);
}

/* Runs a self-check of flow translations in 'ofproto'.  Appends a message to
 * 'reply' describing the results. */
static void
ofproto_dpif_self_check__(struct ofproto_dpif *ofproto, struct ds *reply)
{
    struct cls_cursor cursor;
    struct facet *facet;
    int errors;

    errors = 0;
    ovs_rwlock_rdlock(&ofproto->facets.rwlock);
    cls_cursor_init(&cursor, &ofproto->facets, NULL);
    CLS_CURSOR_FOR_EACH (facet, cr, &cursor) {
        if (!facet_check_consistency(facet)) {
            errors++;
        }
    }
    ovs_rwlock_unlock(&ofproto->facets.rwlock);
    if (errors) {
        ofproto->backer->need_revalidate = REV_INCONSISTENCY;
    }

    if (errors) {
        ds_put_format(reply, "%s: self-check failed (%d errors)\n",
                      ofproto->up.name, errors);
    } else {
        ds_put_format(reply, "%s: self-check passed\n", ofproto->up.name);
    }
}

static void
ofproto_dpif_self_check(struct unixctl_conn *conn,
                        int argc, const char *argv[], void *aux OVS_UNUSED)
{
    struct ds reply = DS_EMPTY_INITIALIZER;
    struct ofproto_dpif *ofproto;

    if (argc > 1) {
        ofproto = ofproto_dpif_lookup(argv[1]);
        if (!ofproto) {
            unixctl_command_reply_error(conn, "Unknown ofproto (use "
                                        "ofproto/list for help)");
            return;
        }
        ofproto_dpif_self_check__(ofproto, &reply);
    } else {
        HMAP_FOR_EACH (ofproto, all_ofproto_dpifs_node, &all_ofproto_dpifs) {
            ofproto_dpif_self_check__(ofproto, &reply);
        }
    }

    unixctl_command_reply(conn, ds_cstr(&reply));
    ds_destroy(&reply);
}

/* Store the current ofprotos in 'ofproto_shash'.  Returns a sorted list
 * of the 'ofproto_shash' nodes.  It is the responsibility of the caller
 * to destroy 'ofproto_shash' and free the returned value. */
static const struct shash_node **
get_ofprotos(struct shash *ofproto_shash)
{
    const struct ofproto_dpif *ofproto;

    HMAP_FOR_EACH (ofproto, all_ofproto_dpifs_node, &all_ofproto_dpifs) {
        char *name = xasprintf("%s@%s", ofproto->up.type, ofproto->up.name);
        shash_add_nocopy(ofproto_shash, name, ofproto);
    }

    return shash_sort(ofproto_shash);
}

static void
ofproto_unixctl_dpif_dump_dps(struct unixctl_conn *conn, int argc OVS_UNUSED,
                              const char *argv[] OVS_UNUSED,
                              void *aux OVS_UNUSED)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct shash ofproto_shash;
    const struct shash_node **sorted_ofprotos;
    int i;

    shash_init(&ofproto_shash);
    sorted_ofprotos = get_ofprotos(&ofproto_shash);
    for (i = 0; i < shash_count(&ofproto_shash); i++) {
        const struct shash_node *node = sorted_ofprotos[i];
        ds_put_format(&ds, "%s\n", node->name);
    }

    shash_destroy(&ofproto_shash);
    free(sorted_ofprotos);

    unixctl_command_reply(conn, ds_cstr(&ds));
    ds_destroy(&ds);
}

static void
show_dp_rates(struct ds *ds, const char *heading,
              const struct avg_subfacet_rates *rates)
{
    ds_put_format(ds, "%s add rate: %5.3f/min, del rate: %5.3f/min\n",
                  heading, rates->add_rate, rates->del_rate);
}

static void
dpif_show_backer(const struct dpif_backer *backer, struct ds *ds)
{
    const struct shash_node **ofprotos;
    struct ofproto_dpif *ofproto;
    struct shash ofproto_shash;
    uint64_t n_hit, n_missed;
    long long int minutes;
    size_t i;

    n_hit = n_missed = 0;
    HMAP_FOR_EACH (ofproto, all_ofproto_dpifs_node, &all_ofproto_dpifs) {
        if (ofproto->backer == backer) {
            n_missed += ofproto->n_missed;
            n_hit += ofproto->n_hit;
        }
    }

    ds_put_format(ds, "%s: hit:%"PRIu64" missed:%"PRIu64"\n",
                  dpif_name(backer->dpif), n_hit, n_missed);
    ds_put_format(ds, "\tflows: cur: %zu, avg: %u, max: %u,"
                  " life span: %lldms\n", hmap_count(&backer->subfacets),
                  backer->avg_n_subfacet, backer->max_n_subfacet,
                  backer->avg_subfacet_life);

    minutes = (time_msec() - backer->created) / (1000 * 60);
    if (minutes >= 60) {
        show_dp_rates(ds, "\thourly avg:", &backer->hourly);
    }
    if (minutes >= 60 * 24) {
        show_dp_rates(ds, "\tdaily avg:",  &backer->daily);
    }
    show_dp_rates(ds, "\toverall avg:",  &backer->lifetime);

    shash_init(&ofproto_shash);
    ofprotos = get_ofprotos(&ofproto_shash);
    for (i = 0; i < shash_count(&ofproto_shash); i++) {
        struct ofproto_dpif *ofproto = ofprotos[i]->data;
        const struct shash_node **ports;
        size_t j;

        if (ofproto->backer != backer) {
            continue;
        }

        ds_put_format(ds, "\t%s: hit:%"PRIu64" missed:%"PRIu64"\n",
                      ofproto->up.name, ofproto->n_hit, ofproto->n_missed);

        ports = shash_sort(&ofproto->up.port_by_name);
        for (j = 0; j < shash_count(&ofproto->up.port_by_name); j++) {
            const struct shash_node *node = ports[j];
            struct ofport *ofport = node->data;
            struct smap config;
            odp_port_t odp_port;

            ds_put_format(ds, "\t\t%s %u/", netdev_get_name(ofport->netdev),
                          ofport->ofp_port);

            odp_port = ofp_port_to_odp_port(ofproto, ofport->ofp_port);
            if (odp_port != ODPP_NONE) {
                ds_put_format(ds, "%"PRIu32":", odp_port);
            } else {
                ds_put_cstr(ds, "none:");
            }

            ds_put_format(ds, " (%s", netdev_get_type(ofport->netdev));

            smap_init(&config);
            if (!netdev_get_config(ofport->netdev, &config)) {
                const struct smap_node **nodes;
                size_t i;

                nodes = smap_sort(&config);
                for (i = 0; i < smap_count(&config); i++) {
                    const struct smap_node *node = nodes[i];
                    ds_put_format(ds, "%c %s=%s", i ? ',' : ':',
                                  node->key, node->value);
                }
                free(nodes);
            }
            smap_destroy(&config);

            ds_put_char(ds, ')');
            ds_put_char(ds, '\n');
        }
        free(ports);
    }
    shash_destroy(&ofproto_shash);
    free(ofprotos);
}

static void
ofproto_unixctl_dpif_show(struct unixctl_conn *conn, int argc OVS_UNUSED,
                          const char *argv[] OVS_UNUSED, void *aux OVS_UNUSED)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    const struct shash_node **backers;
    int i;

    backers = shash_sort(&all_dpif_backers);
    for (i = 0; i < shash_count(&all_dpif_backers); i++) {
        dpif_show_backer(backers[i]->data, &ds);
    }
    free(backers);

    unixctl_command_reply(conn, ds_cstr(&ds));
    ds_destroy(&ds);
}

/* Dump the megaflow (facet) cache.  This is useful to check the
 * correctness of flow wildcarding, since the same mechanism is used for
 * both xlate caching and kernel wildcarding.
 *
 * It's important to note that in the output the flow description uses
 * OpenFlow (OFP) ports, but the actions use datapath (ODP) ports.
 *
 * This command is only needed for advanced debugging, so it's not
 * documented in the man page. */
static void
ofproto_unixctl_dpif_dump_megaflows(struct unixctl_conn *conn,
                                    int argc OVS_UNUSED, const char *argv[],
                                    void *aux OVS_UNUSED)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    const struct ofproto_dpif *ofproto;
    long long int now = time_msec();
    struct cls_cursor cursor;
    struct facet *facet;

    ofproto = ofproto_dpif_lookup(argv[1]);
    if (!ofproto) {
        unixctl_command_reply_error(conn, "no such bridge");
        return;
    }

    ovs_rwlock_rdlock(&ofproto->facets.rwlock);
    cls_cursor_init(&cursor, &ofproto->facets, NULL);
    CLS_CURSOR_FOR_EACH (facet, cr, &cursor) {
        cls_rule_format(&facet->cr, &ds);
        ds_put_cstr(&ds, ", ");
        ds_put_format(&ds, "n_subfacets:%zu, ", list_size(&facet->subfacets));
        ds_put_format(&ds, "used:%.3fs, ", (now - facet->used) / 1000.0);
        ds_put_cstr(&ds, "Datapath actions: ");
        if (facet->xout.slow) {
            uint64_t slow_path_stub[128 / 8];
            const struct nlattr *actions;
            size_t actions_len;

            compose_slow_path(ofproto, &facet->flow, facet->xout.slow,
                              slow_path_stub, sizeof slow_path_stub,
                              &actions, &actions_len);
            format_odp_actions(&ds, actions, actions_len);
        } else {
            format_odp_actions(&ds, facet->xout.odp_actions.data,
                               facet->xout.odp_actions.size);
        }
        ds_put_cstr(&ds, "\n");
    }
    ovs_rwlock_unlock(&ofproto->facets.rwlock);

    ds_chomp(&ds, '\n');
    unixctl_command_reply(conn, ds_cstr(&ds));
    ds_destroy(&ds);
}

/* Disable using the megaflows.
 *
 * This command is only needed for advanced debugging, so it's not
 * documented in the man page. */
static void
ofproto_unixctl_dpif_disable_megaflows(struct unixctl_conn *conn,
                                       int argc OVS_UNUSED,
                                       const char *argv[] OVS_UNUSED,
                                       void *aux OVS_UNUSED)
{
    struct ofproto_dpif *ofproto;

    enable_megaflows = false;

    HMAP_FOR_EACH (ofproto, all_ofproto_dpifs_node, &all_ofproto_dpifs) {
        flush(&ofproto->up);
    }

    unixctl_command_reply(conn, "megaflows disabled");
}

/* Re-enable using megaflows.
 *
 * This command is only needed for advanced debugging, so it's not
 * documented in the man page. */
static void
ofproto_unixctl_dpif_enable_megaflows(struct unixctl_conn *conn,
                                      int argc OVS_UNUSED,
                                      const char *argv[] OVS_UNUSED,
                                      void *aux OVS_UNUSED)
{
    struct ofproto_dpif *ofproto;

    enable_megaflows = true;

    HMAP_FOR_EACH (ofproto, all_ofproto_dpifs_node, &all_ofproto_dpifs) {
        flush(&ofproto->up);
    }

    unixctl_command_reply(conn, "megaflows enabled");
}

static void
ofproto_unixctl_dpif_dump_flows(struct unixctl_conn *conn,
                                int argc OVS_UNUSED, const char *argv[],
                                void *aux OVS_UNUSED)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    const struct ofproto_dpif *ofproto;
    struct subfacet *subfacet;

    ofproto = ofproto_dpif_lookup(argv[1]);
    if (!ofproto) {
        unixctl_command_reply_error(conn, "no such bridge");
        return;
    }

    update_stats(ofproto->backer);

    HMAP_FOR_EACH (subfacet, hmap_node, &ofproto->backer->subfacets) {
        struct facet *facet = subfacet->facet;
        struct odputil_keybuf maskbuf;
        struct ofpbuf mask;

        if (facet->ofproto != ofproto) {
            continue;
        }

        ofpbuf_use_stack(&mask, &maskbuf, sizeof maskbuf);
        if (enable_megaflows) {
            odp_flow_key_from_mask(&mask, &facet->xout.wc.masks,
                                   &facet->flow, UINT32_MAX);
        }

        odp_flow_format(subfacet->key, subfacet->key_len,
                        mask.data, mask.size, &ds, false);

        ds_put_format(&ds, ", packets:%"PRIu64", bytes:%"PRIu64", used:",
                      subfacet->dp_packet_count, subfacet->dp_byte_count);
        if (subfacet->used) {
            ds_put_format(&ds, "%.3fs",
                          (time_msec() - subfacet->used) / 1000.0);
        } else {
            ds_put_format(&ds, "never");
        }
        if (subfacet->facet->tcp_flags) {
            ds_put_cstr(&ds, ", flags:");
            packet_format_tcp_flags(&ds, subfacet->facet->tcp_flags);
        }

        ds_put_cstr(&ds, ", actions:");
        if (facet->xout.slow) {
            uint64_t slow_path_stub[128 / 8];
            const struct nlattr *actions;
            size_t actions_len;

            compose_slow_path(ofproto, &facet->flow, facet->xout.slow,
                              slow_path_stub, sizeof slow_path_stub,
                              &actions, &actions_len);
            format_odp_actions(&ds, actions, actions_len);
        } else {
            format_odp_actions(&ds, facet->xout.odp_actions.data,
                               facet->xout.odp_actions.size);
        }
        ds_put_char(&ds, '\n');
    }

    unixctl_command_reply(conn, ds_cstr(&ds));
    ds_destroy(&ds);
}

static void
ofproto_unixctl_dpif_del_flows(struct unixctl_conn *conn,
                               int argc OVS_UNUSED, const char *argv[],
                               void *aux OVS_UNUSED)
{
    struct ds ds = DS_EMPTY_INITIALIZER;
    struct ofproto_dpif *ofproto;

    ofproto = ofproto_dpif_lookup(argv[1]);
    if (!ofproto) {
        unixctl_command_reply_error(conn, "no such bridge");
        return;
    }

    flush(&ofproto->up);

    unixctl_command_reply(conn, ds_cstr(&ds));
    ds_destroy(&ds);
}

static void
ofproto_dpif_unixctl_init(void)
{
    static bool registered;
    if (registered) {
        return;
    }
    registered = true;

    unixctl_command_register(
        "ofproto/trace",
        "[dp_name]|bridge odp_flow|br_flow [-generate|packet]",
        1, 3, ofproto_unixctl_trace, NULL);
    unixctl_command_register("fdb/flush", "[bridge]", 0, 1,
                             ofproto_unixctl_fdb_flush, NULL);
    unixctl_command_register("fdb/show", "bridge", 1, 1,
                             ofproto_unixctl_fdb_show, NULL);
    unixctl_command_register("ofproto/self-check", "[bridge]", 0, 1,
                             ofproto_dpif_self_check, NULL);
    unixctl_command_register("dpif/dump-dps", "", 0, 0,
                             ofproto_unixctl_dpif_dump_dps, NULL);
    unixctl_command_register("dpif/show", "", 0, 0, ofproto_unixctl_dpif_show,
                             NULL);
    unixctl_command_register("dpif/dump-flows", "bridge", 1, 1,
                             ofproto_unixctl_dpif_dump_flows, NULL);
    unixctl_command_register("dpif/del-flows", "bridge", 1, 1,
                             ofproto_unixctl_dpif_del_flows, NULL);
    unixctl_command_register("dpif/dump-megaflows", "bridge", 1, 1,
                             ofproto_unixctl_dpif_dump_megaflows, NULL);
    unixctl_command_register("dpif/disable-megaflows", "", 0, 0,
                             ofproto_unixctl_dpif_disable_megaflows, NULL);
    unixctl_command_register("dpif/enable-megaflows", "", 0, 0,
                             ofproto_unixctl_dpif_enable_megaflows, NULL);
}

/* Linux VLAN device support (e.g. "eth0.10" for VLAN 10.)
 *
 * This is deprecated.  It is only for compatibility with broken device drivers
 * in old versions of Linux that do not properly support VLANs when VLAN
 * devices are not used.  When broken device drivers are no longer in
 * widespread use, we will delete these interfaces. */

static int
set_realdev(struct ofport *ofport_, ofp_port_t realdev_ofp_port, int vid)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(ofport_->ofproto);
    struct ofport_dpif *ofport = ofport_dpif_cast(ofport_);

    if (realdev_ofp_port == ofport->realdev_ofp_port
        && vid == ofport->vlandev_vid) {
        return 0;
    }

    ofproto->backer->need_revalidate = REV_RECONFIGURE;

    if (ofport->realdev_ofp_port) {
        vsp_remove(ofport);
    }
    if (realdev_ofp_port && ofport->bundle) {
        /* vlandevs are enslaved to their realdevs, so they are not allowed to
         * themselves be part of a bundle. */
        bundle_set(ofport->up.ofproto, ofport->bundle, NULL);
    }

    ofport->realdev_ofp_port = realdev_ofp_port;
    ofport->vlandev_vid = vid;

    if (realdev_ofp_port) {
        vsp_add(ofport, realdev_ofp_port, vid);
    }

    return 0;
}

static uint32_t
hash_realdev_vid(ofp_port_t realdev_ofp_port, int vid)
{
    return hash_2words(ofp_to_u16(realdev_ofp_port), vid);
}

bool
ofproto_has_vlan_splinters(const struct ofproto_dpif *ofproto)
    OVS_EXCLUDED(ofproto->vsp_mutex)
{
    bool ret;

    ovs_mutex_lock(&ofproto->vsp_mutex);
    ret = !hmap_is_empty(&ofproto->realdev_vid_map);
    ovs_mutex_unlock(&ofproto->vsp_mutex);
    return ret;
}

static ofp_port_t
vsp_realdev_to_vlandev__(const struct ofproto_dpif *ofproto,
                         ofp_port_t realdev_ofp_port, ovs_be16 vlan_tci)
    OVS_REQUIRES(ofproto->vsp_mutex)
{
    if (!hmap_is_empty(&ofproto->realdev_vid_map)) {
        int vid = vlan_tci_to_vid(vlan_tci);
        const struct vlan_splinter *vsp;

        HMAP_FOR_EACH_WITH_HASH (vsp, realdev_vid_node,
                                 hash_realdev_vid(realdev_ofp_port, vid),
                                 &ofproto->realdev_vid_map) {
            if (vsp->realdev_ofp_port == realdev_ofp_port
                && vsp->vid == vid) {
                return vsp->vlandev_ofp_port;
            }
        }
    }
    return realdev_ofp_port;
}

/* Returns the OFP port number of the Linux VLAN device that corresponds to
 * 'vlan_tci' on the network device with port number 'realdev_ofp_port' in
 * 'struct ofport_dpif'.  For example, given 'realdev_ofp_port' of eth0 and
 * 'vlan_tci' 9, it would return the port number of eth0.9.
 *
 * Unless VLAN splinters are enabled for port 'realdev_ofp_port', this
 * function just returns its 'realdev_ofp_port' argument. */
ofp_port_t
vsp_realdev_to_vlandev(const struct ofproto_dpif *ofproto,
                       ofp_port_t realdev_ofp_port, ovs_be16 vlan_tci)
    OVS_EXCLUDED(ofproto->vsp_mutex)
{
    ofp_port_t ret;

    ovs_mutex_lock(&ofproto->vsp_mutex);
    ret = vsp_realdev_to_vlandev__(ofproto, realdev_ofp_port, vlan_tci);
    ovs_mutex_unlock(&ofproto->vsp_mutex);
    return ret;
}

static struct vlan_splinter *
vlandev_find(const struct ofproto_dpif *ofproto, ofp_port_t vlandev_ofp_port)
{
    struct vlan_splinter *vsp;

    HMAP_FOR_EACH_WITH_HASH (vsp, vlandev_node,
                             hash_ofp_port(vlandev_ofp_port),
                             &ofproto->vlandev_map) {
        if (vsp->vlandev_ofp_port == vlandev_ofp_port) {
            return vsp;
        }
    }

    return NULL;
}

/* Returns the OpenFlow port number of the "real" device underlying the Linux
 * VLAN device with OpenFlow port number 'vlandev_ofp_port' and stores the
 * VLAN VID of the Linux VLAN device in '*vid'.  For example, given
 * 'vlandev_ofp_port' of eth0.9, it would return the OpenFlow port number of
 * eth0 and store 9 in '*vid'.
 *
 * Returns 0 and does not modify '*vid' if 'vlandev_ofp_port' is not a Linux
 * VLAN device.  Unless VLAN splinters are enabled, this is what this function
 * always does.*/
static ofp_port_t
vsp_vlandev_to_realdev(const struct ofproto_dpif *ofproto,
                       ofp_port_t vlandev_ofp_port, int *vid)
    OVS_REQUIRES(ofproto->vsp_mutex)
{
    if (!hmap_is_empty(&ofproto->vlandev_map)) {
        const struct vlan_splinter *vsp;

        vsp = vlandev_find(ofproto, vlandev_ofp_port);
        if (vsp) {
            if (vid) {
                *vid = vsp->vid;
            }
            return vsp->realdev_ofp_port;
        }
    }
    return 0;
}

/* Given 'flow', a flow representing a packet received on 'ofproto', checks
 * whether 'flow->in_port' represents a Linux VLAN device.  If so, changes
 * 'flow->in_port' to the "real" device backing the VLAN device, sets
 * 'flow->vlan_tci' to the VLAN VID, and returns true.  Otherwise (which is
 * always the case unless VLAN splinters are enabled), returns false without
 * making any changes. */
bool
vsp_adjust_flow(const struct ofproto_dpif *ofproto, struct flow *flow)
    OVS_EXCLUDED(ofproto->vsp_mutex)
{
    ofp_port_t realdev;
    int vid;

    ovs_mutex_lock(&ofproto->vsp_mutex);
    realdev = vsp_vlandev_to_realdev(ofproto, flow->in_port.ofp_port, &vid);
    ovs_mutex_unlock(&ofproto->vsp_mutex);
    if (!realdev) {
        return false;
    }

    /* Cause the flow to be processed as if it came in on the real device with
     * the VLAN device's VLAN ID. */
    flow->in_port.ofp_port = realdev;
    flow->vlan_tci = htons((vid & VLAN_VID_MASK) | VLAN_CFI);
    return true;
}

static void
vsp_remove(struct ofport_dpif *port)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(port->up.ofproto);
    struct vlan_splinter *vsp;

    ovs_mutex_lock(&ofproto->vsp_mutex);
    vsp = vlandev_find(ofproto, port->up.ofp_port);
    if (vsp) {
        hmap_remove(&ofproto->vlandev_map, &vsp->vlandev_node);
        hmap_remove(&ofproto->realdev_vid_map, &vsp->realdev_vid_node);
        free(vsp);

        port->realdev_ofp_port = 0;
    } else {
        VLOG_ERR("missing vlan device record");
    }
    ovs_mutex_unlock(&ofproto->vsp_mutex);
}

static void
vsp_add(struct ofport_dpif *port, ofp_port_t realdev_ofp_port, int vid)
{
    struct ofproto_dpif *ofproto = ofproto_dpif_cast(port->up.ofproto);

    ovs_mutex_lock(&ofproto->vsp_mutex);
    if (!vsp_vlandev_to_realdev(ofproto, port->up.ofp_port, NULL)
        && (vsp_realdev_to_vlandev__(ofproto, realdev_ofp_port, htons(vid))
            == realdev_ofp_port)) {
        struct vlan_splinter *vsp;

        vsp = xmalloc(sizeof *vsp);
        vsp->realdev_ofp_port = realdev_ofp_port;
        vsp->vlandev_ofp_port = port->up.ofp_port;
        vsp->vid = vid;

        port->realdev_ofp_port = realdev_ofp_port;

        hmap_insert(&ofproto->vlandev_map, &vsp->vlandev_node,
                    hash_ofp_port(port->up.ofp_port));
        hmap_insert(&ofproto->realdev_vid_map, &vsp->realdev_vid_node,
                    hash_realdev_vid(realdev_ofp_port, vid));
    } else {
        VLOG_ERR("duplicate vlan device record");
    }
    ovs_mutex_unlock(&ofproto->vsp_mutex);
}

static odp_port_t
ofp_port_to_odp_port(const struct ofproto_dpif *ofproto, ofp_port_t ofp_port)
{
    const struct ofport_dpif *ofport = get_ofp_port(ofproto, ofp_port);
    return ofport ? ofport->odp_port : ODPP_NONE;
}

struct ofport_dpif *
odp_port_to_ofport(const struct dpif_backer *backer, odp_port_t odp_port)
{
    struct ofport_dpif *port;

    ovs_rwlock_rdlock(&backer->odp_to_ofport_lock);
    HMAP_FOR_EACH_IN_BUCKET (port, odp_port_node, hash_odp_port(odp_port),
                             &backer->odp_to_ofport_map) {
        if (port->odp_port == odp_port) {
            ovs_rwlock_unlock(&backer->odp_to_ofport_lock);
            return port;
        }
    }

    ovs_rwlock_unlock(&backer->odp_to_ofport_lock);
    return NULL;
}

static ofp_port_t
odp_port_to_ofp_port(const struct ofproto_dpif *ofproto, odp_port_t odp_port)
{
    struct ofport_dpif *port;

    port = odp_port_to_ofport(ofproto->backer, odp_port);
    if (port && &ofproto->up == port->up.ofproto) {
        return port->up.ofp_port;
    } else {
        return OFPP_NONE;
    }
}

/* Compute exponentially weighted moving average, adding 'new' as the newest,
 * most heavily weighted element.  'base' designates the rate of decay: after
 * 'base' further updates, 'new''s weight in the EWMA decays to about 1/e
 * (about .37). */
static void
exp_mavg(double *avg, int base, double new)
{
    *avg = (*avg * (base - 1) + new) / base;
}

static void
update_moving_averages(struct dpif_backer *backer)
{
    const int min_ms = 60 * 1000; /* milliseconds in one minute. */
    long long int minutes = (time_msec() - backer->created) / min_ms;

    if (minutes > 0) {
        backer->lifetime.add_rate = (double) backer->total_subfacet_add_count
            / minutes;
        backer->lifetime.del_rate = (double) backer->total_subfacet_del_count
            / minutes;
    } else {
        backer->lifetime.add_rate = 0.0;
        backer->lifetime.del_rate = 0.0;
    }

    /* Update hourly averages on the minute boundaries. */
    if (time_msec() - backer->last_minute >= min_ms) {
        exp_mavg(&backer->hourly.add_rate, 60, backer->subfacet_add_count);
        exp_mavg(&backer->hourly.del_rate, 60, backer->subfacet_del_count);

        /* Update daily averages on the hour boundaries. */
        if ((backer->last_minute - backer->created) / min_ms % 60 == 59) {
            exp_mavg(&backer->daily.add_rate, 24, backer->hourly.add_rate);
            exp_mavg(&backer->daily.del_rate, 24, backer->hourly.del_rate);
        }

        backer->total_subfacet_add_count += backer->subfacet_add_count;
        backer->total_subfacet_del_count += backer->subfacet_del_count;
        backer->subfacet_add_count = 0;
        backer->subfacet_del_count = 0;
        backer->last_minute += min_ms;
    }
}

const struct ofproto_class ofproto_dpif_class = {
    init,
    enumerate_types,
    enumerate_names,
    del,
    port_open_type,
    type_run,
    type_run_fast,
    type_wait,
    alloc,
    construct,
    destruct,
    dealloc,
    run,
    run_fast,
    wait,
    get_memory_usage,
    flush,
    get_features,
    get_tables,
    port_alloc,
    port_construct,
    port_destruct,
    port_dealloc,
    port_modified,
    port_reconfigured,
    port_query_by_name,
    port_add,
    port_del,
    port_get_stats,
    port_dump_start,
    port_dump_next,
    port_dump_done,
    port_poll,
    port_poll_wait,
    port_is_lacp_current,
    NULL,                       /* rule_choose_table */
    rule_alloc,
    rule_construct,
    rule_insert,
    rule_delete,
    rule_destruct,
    rule_dealloc,
    rule_get_stats,
    rule_execute,
    rule_modify_actions,
    set_frag_handling,
    packet_out,
    set_netflow,
    get_netflow_ids,
    set_sflow,
    set_ipfix,
    set_cfm,
    get_cfm_status,
    set_bfd,
    get_bfd_status,
    set_stp,
    get_stp_status,
    set_stp_port,
    get_stp_port_status,
    set_queues,
    bundle_set,
    bundle_remove,
    mirror_set__,
    mirror_get_stats__,
    set_flood_vlans,
    is_mirror_output_bundle,
    forward_bpdu_changed,
    set_mac_table_config,
    set_realdev,
    NULL,                       /* meter_get_features */
    NULL,                       /* meter_set */
    NULL,                       /* meter_get */
    NULL,                       /* meter_del */
};
