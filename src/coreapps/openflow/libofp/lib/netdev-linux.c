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

#include "netdev-linux.h"

#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <linux/filter.h>
#include <linux/gen_stats.h>
#include <linux/if_ether.h>
#include <linux/if_tun.h>
#include <linux/types.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/pkt_cls.h>
#include <linux/pkt_sched.h>
#include <linux/rtnetlink.h>
#include <linux/sockios.h>
#include <linux/version.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netpacket/packet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_packet.h>
#include <net/route.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "coverage.h"
#include "dpif-linux.h"
#include "dynamic-string.h"
#include "fatal-signal.h"
#include "hash.h"
#include "hmap.h"
#include "netdev-provider.h"
#include "netdev-vport.h"
#include "netlink-notifier.h"
#include "netlink-socket.h"
#include "netlink.h"
#include "ofpbuf.h"
#include "openflow/openflow.h"
#include "packets.h"
#include "poll-loop.h"
#include "rtnetlink-link.h"
#include "shash.h"
#include "socket-util.h"
#include "sset.h"
#include "timer.h"
#include "unaligned.h"
#include "vlog.h"

VLOG_DEFINE_THIS_MODULE(netdev_linux);

COVERAGE_DEFINE(netdev_set_policing);
COVERAGE_DEFINE(netdev_arp_lookup);
COVERAGE_DEFINE(netdev_get_ifindex);
COVERAGE_DEFINE(netdev_get_hwaddr);
COVERAGE_DEFINE(netdev_set_hwaddr);
COVERAGE_DEFINE(netdev_get_ethtool);
COVERAGE_DEFINE(netdev_set_ethtool);


/* These were introduced in Linux 2.6.14, so they might be missing if we have
 * old headers. */
#ifndef ADVERTISED_Pause
#define ADVERTISED_Pause                (1 << 13)
#endif
#ifndef ADVERTISED_Asym_Pause
#define ADVERTISED_Asym_Pause           (1 << 14)
#endif

/* These were introduced in Linux 2.6.24, so they might be missing if we
 * have old headers. */
#ifndef ETHTOOL_GFLAGS
#define ETHTOOL_GFLAGS       0x00000025 /* Get flags bitmap(ethtool_value) */
#endif
#ifndef ETHTOOL_SFLAGS
#define ETHTOOL_SFLAGS       0x00000026 /* Set flags bitmap(ethtool_value) */
#endif

/* This was introduced in Linux 2.6.25, so it might be missing if we have old
 * headers. */
#ifndef TC_RTAB_SIZE
#define TC_RTAB_SIZE 1024
#endif

enum {
    VALID_IFINDEX           = 1 << 0,
    VALID_ETHERADDR         = 1 << 1,
    VALID_IN4               = 1 << 2,
    VALID_IN6               = 1 << 3,
    VALID_MTU               = 1 << 4,
    VALID_POLICING          = 1 << 5,
    VALID_VPORT_STAT_ERROR  = 1 << 6,
    VALID_DRVINFO           = 1 << 7,
    VALID_FEATURES          = 1 << 8,
};

/* Traffic control. */

/* An instance of a traffic control class.  Always associated with a particular
 * network device.
 *
 * Each TC implementation subclasses this with whatever additional data it
 * needs. */
struct tc {
    const struct tc_ops *ops;
    struct hmap queues;         /* Contains "struct tc_queue"s.
                                 * Read by generic TC layer.
                                 * Written only by TC implementation. */
};

#define TC_INITIALIZER(TC, OPS) { OPS, HMAP_INITIALIZER(&(TC)->queues) }

/* One traffic control queue.
 *
 * Each TC implementation subclasses this with whatever additional data it
 * needs. */
struct tc_queue {
    struct hmap_node hmap_node; /* In struct tc's "queues" hmap. */
    unsigned int queue_id;      /* OpenFlow queue ID. */
    long long int created;      /* Time queue was created, in msecs. */
};

/* A particular kind of traffic control.  Each implementation generally maps to
 * one particular Linux qdisc class.
 *
 * The functions below return 0 if successful or a positive errno value on
 * failure, except where otherwise noted.  All of them must be provided, except
 * where otherwise noted. */
struct tc_ops {
    /* Name used by kernel in the TCA_KIND attribute of tcmsg, e.g. "htb".
     * This is null for tc_ops_default and tc_ops_other, for which there are no
     * appropriate values. */
    const char *linux_name;

    /* Name used in OVS database, e.g. "linux-htb".  Must be nonnull. */
    const char *ovs_name;

    /* Number of supported OpenFlow queues, 0 for qdiscs that have no
     * queues.  The queues are numbered 0 through n_queues - 1. */
    unsigned int n_queues;

    /* Called to install this TC class on 'netdev'.  The implementation should
     * make the Netlink calls required to set up 'netdev' with the right qdisc
     * and configure it according to 'details'.  The implementation may assume
     * that the current qdisc is the default; that is, there is no need for it
     * to delete the current qdisc before installing itself.
     *
     * The contents of 'details' should be documented as valid for 'ovs_name'
     * in the "other_config" column in the "QoS" table in vswitchd/vswitch.xml
     * (which is built as ovs-vswitchd.conf.db(8)).
     *
     * This function must return 0 if and only if it sets 'netdev->tc' to an
     * initialized 'struct tc'.
     *
     * (This function is null for tc_ops_other, which cannot be installed.  For
     * other TC classes it should always be nonnull.) */
    int (*tc_install)(struct netdev *netdev, const struct smap *details);

    /* Called when the netdev code determines (through a Netlink query) that
     * this TC class's qdisc is installed on 'netdev', but we didn't install
     * it ourselves and so don't know any of the details.
     *
     * 'nlmsg' is the kernel reply to a RTM_GETQDISC Netlink message for
     * 'netdev'.  The TCA_KIND attribute of 'nlmsg' is 'linux_name'.  The
     * implementation should parse the other attributes of 'nlmsg' as
     * necessary to determine its configuration.  If necessary it should also
     * use Netlink queries to determine the configuration of queues on
     * 'netdev'.
     *
     * This function must return 0 if and only if it sets 'netdev->tc' to an
     * initialized 'struct tc'. */
    int (*tc_load)(struct netdev *netdev, struct ofpbuf *nlmsg);

    /* Destroys the data structures allocated by the implementation as part of
     * 'tc'.  (This includes destroying 'tc->queues' by calling
     * tc_destroy(tc).
     *
     * The implementation should not need to perform any Netlink calls.  If
     * desirable, the caller is responsible for deconfiguring the kernel qdisc.
     * (But it may not be desirable.)
     *
     * This function may be null if 'tc' is trivial. */
    void (*tc_destroy)(struct tc *tc);

    /* Retrieves details of 'netdev->tc' configuration into 'details'.
     *
     * The implementation should not need to perform any Netlink calls, because
     * the 'tc_install' or 'tc_load' that instantiated 'netdev->tc' should have
     * cached the configuration.
     *
     * The contents of 'details' should be documented as valid for 'ovs_name'
     * in the "other_config" column in the "QoS" table in vswitchd/vswitch.xml
     * (which is built as ovs-vswitchd.conf.db(8)).
     *
     * This function may be null if 'tc' is not configurable.
     */
    int (*qdisc_get)(const struct netdev *netdev, struct smap *details);

    /* Reconfigures 'netdev->tc' according to 'details', performing any
     * required Netlink calls to complete the reconfiguration.
     *
     * The contents of 'details' should be documented as valid for 'ovs_name'
     * in the "other_config" column in the "QoS" table in vswitchd/vswitch.xml
     * (which is built as ovs-vswitchd.conf.db(8)).
     *
     * This function may be null if 'tc' is not configurable.
     */
    int (*qdisc_set)(struct netdev *, const struct smap *details);

    /* Retrieves details of 'queue' on 'netdev->tc' into 'details'.  'queue' is
     * one of the 'struct tc_queue's within 'netdev->tc->queues'.
     *
     * The contents of 'details' should be documented as valid for 'ovs_name'
     * in the "other_config" column in the "Queue" table in
     * vswitchd/vswitch.xml (which is built as ovs-vswitchd.conf.db(8)).
     *
     * The implementation should not need to perform any Netlink calls, because
     * the 'tc_install' or 'tc_load' that instantiated 'netdev->tc' should have
     * cached the queue configuration.
     *
     * This function may be null if 'tc' does not have queues ('n_queues' is
     * 0). */
    int (*class_get)(const struct netdev *netdev, const struct tc_queue *queue,
                     struct smap *details);

    /* Configures or reconfigures 'queue_id' on 'netdev->tc' according to
     * 'details', perfoming any required Netlink calls to complete the
     * reconfiguration.  The caller ensures that 'queue_id' is less than
     * 'n_queues'.
     *
     * The contents of 'details' should be documented as valid for 'ovs_name'
     * in the "other_config" column in the "Queue" table in
     * vswitchd/vswitch.xml (which is built as ovs-vswitchd.conf.db(8)).
     *
     * This function may be null if 'tc' does not have queues or its queues are
     * not configurable. */
    int (*class_set)(struct netdev *, unsigned int queue_id,
                     const struct smap *details);

    /* Deletes 'queue' from 'netdev->tc'.  'queue' is one of the 'struct
     * tc_queue's within 'netdev->tc->queues'.
     *
     * This function may be null if 'tc' does not have queues or its queues
     * cannot be deleted. */
    int (*class_delete)(struct netdev *, struct tc_queue *queue);

    /* Obtains stats for 'queue' from 'netdev->tc'.  'queue' is one of the
     * 'struct tc_queue's within 'netdev->tc->queues'.
     *
     * On success, initializes '*stats'.
     *
     * This function may be null if 'tc' does not have queues or if it cannot
     * report queue statistics. */
    int (*class_get_stats)(const struct netdev *netdev,
                           const struct tc_queue *queue,
                           struct netdev_queue_stats *stats);

    /* Extracts queue stats from 'nlmsg', which is a response to a
     * RTM_GETTCLASS message, and passes them to 'cb' along with 'aux'.
     *
     * This function may be null if 'tc' does not have queues or if it cannot
     * report queue statistics. */
    int (*class_dump_stats)(const struct netdev *netdev,
                            const struct ofpbuf *nlmsg,
                            netdev_dump_queue_stats_cb *cb, void *aux);
};

static void
tc_init(struct tc *tc, const struct tc_ops *ops)
{
    tc->ops = ops;
    hmap_init(&tc->queues);
}

static void
tc_destroy(struct tc *tc)
{
    hmap_destroy(&tc->queues);
}

static const struct tc_ops tc_ops_htb;
static const struct tc_ops tc_ops_hfsc;
static const struct tc_ops tc_ops_default;
static const struct tc_ops tc_ops_other;

static const struct tc_ops *const tcs[] = {
    &tc_ops_htb,                /* Hierarchy token bucket (see tc-htb(8)). */
    &tc_ops_hfsc,               /* Hierarchical fair service curve. */
    &tc_ops_default,            /* Default qdisc (see tc-pfifo_fast(8)). */
    &tc_ops_other,              /* Some other qdisc. */
    NULL
};

static unsigned int tc_make_handle(unsigned int major, unsigned int minor);
static unsigned int tc_get_major(unsigned int handle);
static unsigned int tc_get_minor(unsigned int handle);

static unsigned int tc_ticks_to_bytes(unsigned int rate, unsigned int ticks);
static unsigned int tc_bytes_to_ticks(unsigned int rate, unsigned int size);
static unsigned int tc_buffer_per_jiffy(unsigned int rate);

static struct tcmsg *tc_make_request(const struct netdev *, int type,
                                     unsigned int flags, struct ofpbuf *);
static int tc_transact(struct ofpbuf *request, struct ofpbuf **replyp);
static int tc_add_del_ingress_qdisc(struct netdev *netdev, bool add);
static int tc_add_policer(struct netdev *netdev, int kbits_rate,
                          int kbits_burst);

static int tc_parse_qdisc(const struct ofpbuf *, const char **kind,
                          struct nlattr **options);
static int tc_parse_class(const struct ofpbuf *, unsigned int *queue_id,
                          struct nlattr **options,
                          struct netdev_queue_stats *);
static int tc_query_class(const struct netdev *,
                          unsigned int handle, unsigned int parent,
                          struct ofpbuf **replyp);
static int tc_delete_class(const struct netdev *, unsigned int handle);

static int tc_del_qdisc(struct netdev *netdev);
static int tc_query_qdisc(const struct netdev *netdev);

static int tc_calc_cell_log(unsigned int mtu);
static void tc_fill_rate(struct tc_ratespec *rate, uint64_t bps, int mtu);
static void tc_put_rtab(struct ofpbuf *, uint16_t type,
                        const struct tc_ratespec *rate);
static int tc_calc_buffer(unsigned int Bps, int mtu, uint64_t burst_bytes);

struct netdev_linux {
    struct netdev up;

    /* Protects all members below. */
    struct ovs_mutex mutex;

    unsigned int cache_valid;
    unsigned int change_seq;

    bool miimon;                    /* Link status of last poll. */
    long long int miimon_interval;  /* Miimon Poll rate. Disabled if <= 0. */
    struct timer miimon_timer;

    /* The following are figured out "on demand" only.  They are only valid
     * when the corresponding VALID_* bit in 'cache_valid' is set. */
    int ifindex;
    uint8_t etheraddr[ETH_ADDR_LEN];
    struct in_addr address, netmask;
    struct in6_addr in6;
    int mtu;
    unsigned int ifi_flags;
    long long int carrier_resets;
    uint32_t kbits_rate;        /* Policing data. */
    uint32_t kbits_burst;
    int vport_stats_error;      /* Cached error code from vport_get_stats().
                                   0 or an errno value. */
    int netdev_mtu_error;       /* Cached error code from SIOCGIFMTU or SIOCSIFMTU. */
    int ether_addr_error;       /* Cached error code from set/get etheraddr. */
    int netdev_policing_error;  /* Cached error code from set policing. */
    int get_features_error;     /* Cached error code from ETHTOOL_GSET. */
    int get_ifindex_error;      /* Cached error code from SIOCGIFINDEX. */

    enum netdev_features current;    /* Cached from ETHTOOL_GSET. */
    enum netdev_features advertised; /* Cached from ETHTOOL_GSET. */
    enum netdev_features supported;  /* Cached from ETHTOOL_GSET. */

    struct ethtool_drvinfo drvinfo;  /* Cached from ETHTOOL_GDRVINFO. */
    struct tc *tc;

    /* For devices of class netdev_tap_class only. */
    int tap_fd;
};

struct netdev_rx_linux {
    struct netdev_rx up;
    bool is_tap;
    int fd;
};

/* This is set pretty low because we probably won't learn anything from the
 * additional log messages. */
static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(5, 20);

static void netdev_linux_run(void);

static int netdev_linux_do_ethtool(const char *name, struct ethtool_cmd *,
                                   int cmd, const char *cmd_name);
static int netdev_linux_get_ipv4(const struct netdev *, struct in_addr *,
                                 int cmd, const char *cmd_name);
static int get_flags(const struct netdev *, unsigned int *flags);
static int set_flags(const char *, unsigned int flags);
static int update_flags(struct netdev_linux *netdev, enum netdev_flags off,
                        enum netdev_flags on, enum netdev_flags *old_flagsp)
    OVS_REQUIRES(netdev->mutex);
static int do_get_ifindex(const char *netdev_name);
static int get_ifindex(const struct netdev *, int *ifindexp);
static int do_set_addr(struct netdev *netdev,
                       int ioctl_nr, const char *ioctl_name,
                       struct in_addr addr);
static int get_etheraddr(const char *netdev_name, uint8_t ea[ETH_ADDR_LEN]);
static int set_etheraddr(const char *netdev_name, const uint8_t[ETH_ADDR_LEN]);
static int get_stats_via_netlink(int ifindex, struct netdev_stats *stats);
static int get_stats_via_proc(const char *netdev_name, struct netdev_stats *stats);
static int af_packet_sock(void);
static void netdev_linux_miimon_run(void);
static void netdev_linux_miimon_wait(void);

static bool
is_netdev_linux_class(const struct netdev_class *netdev_class)
{
    return netdev_class->run == netdev_linux_run;
}

static bool
is_tap_netdev(const struct netdev *netdev)
{
    return netdev_get_class(netdev) == &netdev_tap_class;
}

static struct netdev_linux *
netdev_linux_cast(const struct netdev *netdev)
{
    ovs_assert(is_netdev_linux_class(netdev_get_class(netdev)));

    return CONTAINER_OF(netdev, struct netdev_linux, up);
}

static struct netdev_rx_linux *
netdev_rx_linux_cast(const struct netdev_rx *rx)
{
    ovs_assert(is_netdev_linux_class(netdev_get_class(rx->netdev)));
    return CONTAINER_OF(rx, struct netdev_rx_linux, up);
}

static void netdev_linux_update(struct netdev_linux *netdev,
                                const struct rtnetlink_link_change *)
    OVS_REQUIRES(netdev->mutex);
static void netdev_linux_changed(struct netdev_linux *netdev,
                                 unsigned int ifi_flags, unsigned int mask)
    OVS_REQUIRES(netdev->mutex);

/* Returns a NETLINK_ROUTE socket listening for RTNLGRP_LINK changes, or NULL
 * if no such socket could be created. */
static struct nl_sock *
netdev_linux_notify_sock(void)
{
    static struct ovsthread_once once = OVSTHREAD_ONCE_INITIALIZER;
    static struct nl_sock *sock;

    if (ovsthread_once_start(&once)) {
        int error;

        error = nl_sock_create(NETLINK_ROUTE, &sock);
        if (!error) {
            error = nl_sock_join_mcgroup(sock, RTNLGRP_LINK);
            if (error) {
                nl_sock_destroy(sock);
                sock = NULL;
            }
        }
        ovsthread_once_done(&once);
    }

    return sock;
}

static void
netdev_linux_run(void)
{
    struct nl_sock *sock;
    int error;

    netdev_linux_miimon_run();

    sock = netdev_linux_notify_sock();
    if (!sock) {
        return;
    }

    do {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
        uint64_t buf_stub[4096 / 8];
        struct ofpbuf buf;

        ofpbuf_use_stub(&buf, buf_stub, sizeof buf_stub);
        error = nl_sock_recv(sock, &buf, false);
        if (!error) {
            struct rtnetlink_link_change change;

            if (rtnetlink_link_parse(&buf, &change)) {
                struct netdev *netdev_ = netdev_from_name(change.ifname);
                if (netdev_ && is_netdev_linux_class(netdev_->netdev_class)) {
                    struct netdev_linux *netdev = netdev_linux_cast(netdev_);

                    ovs_mutex_lock(&netdev->mutex);
                    netdev_linux_update(netdev, &change);
                    ovs_mutex_unlock(&netdev->mutex);
                }
                netdev_close(netdev_);
            }
        } else if (error == ENOBUFS) {
            struct shash device_shash;
            struct shash_node *node;

            nl_sock_drain(sock);

            shash_init(&device_shash);
            netdev_get_devices(&netdev_linux_class, &device_shash);
            SHASH_FOR_EACH (node, &device_shash) {
                struct netdev *netdev_ = node->data;
                struct netdev_linux *netdev = netdev_linux_cast(netdev_);
                unsigned int flags;

                ovs_mutex_lock(&netdev->mutex);
                get_flags(netdev_, &flags);
                netdev_linux_changed(netdev, flags, 0);
                ovs_mutex_unlock(&netdev->mutex);

                netdev_close(netdev_);
            }
            shash_destroy(&device_shash);
        } else if (error != EAGAIN) {
            VLOG_WARN_RL(&rl, "error reading or parsing netlink (%s)",
                         ovs_strerror(error));
        }
        ofpbuf_uninit(&buf);
    } while (!error);
}

static void
netdev_linux_wait(void)
{
    struct nl_sock *sock;

    netdev_linux_miimon_wait();
    sock = netdev_linux_notify_sock();
    if (sock) {
        nl_sock_wait(sock, POLLIN);
    }
}

static void
netdev_linux_changed(struct netdev_linux *dev,
                     unsigned int ifi_flags, unsigned int mask)
    OVS_REQUIRES(dev->mutex)
{
    dev->change_seq++;
    if (!dev->change_seq) {
        dev->change_seq++;
    }

    if ((dev->ifi_flags ^ ifi_flags) & IFF_RUNNING) {
        dev->carrier_resets++;
    }
    dev->ifi_flags = ifi_flags;

    dev->cache_valid &= mask;
}

static void
netdev_linux_update(struct netdev_linux *dev,
                    const struct rtnetlink_link_change *change)
    OVS_REQUIRES(dev->mutex)
{
    if (change->nlmsg_type == RTM_NEWLINK) {
        /* Keep drv-info */
        netdev_linux_changed(dev, change->ifi_flags, VALID_DRVINFO);

        /* Update netdev from rtnl-change msg. */
        if (change->mtu) {
            dev->mtu = change->mtu;
            dev->cache_valid |= VALID_MTU;
            dev->netdev_mtu_error = 0;
        }

        if (!eth_addr_is_zero(change->addr)) {
            memcpy(dev->etheraddr, change->addr, ETH_ADDR_LEN);
            dev->cache_valid |= VALID_ETHERADDR;
            dev->ether_addr_error = 0;
        }

        dev->ifindex = change->ifi_index;
        dev->cache_valid |= VALID_IFINDEX;
        dev->get_ifindex_error = 0;

    } else {
        netdev_linux_changed(dev, change->ifi_flags, 0);
    }
}

static struct netdev *
netdev_linux_alloc(void)
{
    struct netdev_linux *netdev = xzalloc(sizeof *netdev);
    return &netdev->up;
}

static void
netdev_linux_common_construct(struct netdev_linux *netdev)
{
    ovs_mutex_init(&netdev->mutex);
    netdev->change_seq = 1;
}

/* Creates system and internal devices. */
static int
netdev_linux_construct(struct netdev *netdev_)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    int error;

    netdev_linux_common_construct(netdev);

    error = get_flags(&netdev->up, &netdev->ifi_flags);
    if (error == ENODEV) {
        if (netdev->up.netdev_class != &netdev_internal_class) {
            /* The device does not exist, so don't allow it to be opened. */
            return ENODEV;
        } else {
            /* "Internal" netdevs have to be created as netdev objects before
             * they exist in the kernel, because creating them in the kernel
             * happens by passing a netdev object to dpif_port_add().
             * Therefore, ignore the error. */
        }
    }

    return 0;
}

/* For most types of netdevs we open the device for each call of
 * netdev_open().  However, this is not the case with tap devices,
 * since it is only possible to open the device once.  In this
 * situation we share a single file descriptor, and consequently
 * buffers, across all readers.  Therefore once data is read it will
 * be unavailable to other reads for tap devices. */
static int
netdev_linux_construct_tap(struct netdev *netdev_)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    static const char tap_dev[] = "/dev/net/tun";
    const char *name = netdev_->name;
    struct ifreq ifr;
    int error;

    netdev_linux_common_construct(netdev);

    /* Open tap device. */
    netdev->tap_fd = open(tap_dev, O_RDWR);
    if (netdev->tap_fd < 0) {
        error = errno;
        VLOG_WARN("opening \"%s\" failed: %s", tap_dev, ovs_strerror(error));
        return error;
    }

    /* Create tap device. */
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    ovs_strzcpy(ifr.ifr_name, name, sizeof ifr.ifr_name);
    if (ioctl(netdev->tap_fd, TUNSETIFF, &ifr) == -1) {
        VLOG_WARN("%s: creating tap device failed: %s", name,
                  ovs_strerror(errno));
        error = errno;
        goto error_close;
    }

    /* Make non-blocking. */
    error = set_nonblocking(netdev->tap_fd);
    if (error) {
        goto error_close;
    }

    return 0;

error_close:
    close(netdev->tap_fd);
    return error;
}

static void
netdev_linux_destruct(struct netdev *netdev_)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);

    if (netdev->tc && netdev->tc->ops->tc_destroy) {
        netdev->tc->ops->tc_destroy(netdev->tc);
    }

    if (netdev_get_class(netdev_) == &netdev_tap_class
        && netdev->tap_fd >= 0)
    {
        close(netdev->tap_fd);
    }

    ovs_mutex_destroy(&netdev->mutex);
}

static void
netdev_linux_dealloc(struct netdev *netdev_)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    free(netdev);
}

static struct netdev_rx *
netdev_linux_rx_alloc(void)
{
    struct netdev_rx_linux *rx = xzalloc(sizeof *rx);
    return &rx->up;
}

static int
netdev_linux_rx_construct(struct netdev_rx *rx_)
{
    struct netdev_rx_linux *rx = netdev_rx_linux_cast(rx_);
    struct netdev *netdev_ = rx->up.netdev;
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    int error;

    ovs_mutex_lock(&netdev->mutex);
    rx->is_tap = is_tap_netdev(netdev_);
    if (rx->is_tap) {
        rx->fd = netdev->tap_fd;
    } else {
        struct sockaddr_ll sll;
        int ifindex;
        /* Result of tcpdump -dd inbound */
        static const struct sock_filter filt[] = {
            { 0x28, 0, 0, 0xfffff004 }, /* ldh [0] */
            { 0x15, 0, 1, 0x00000004 }, /* jeq #4     jt 2  jf 3 */
            { 0x6, 0, 0, 0x00000000 },  /* ret #0 */
            { 0x6, 0, 0, 0x0000ffff }   /* ret #65535 */
        };
        static const struct sock_fprog fprog = {
            ARRAY_SIZE(filt), (struct sock_filter *) filt
        };

        /* Create file descriptor. */
        rx->fd = socket(PF_PACKET, SOCK_RAW, 0);
        if (rx->fd < 0) {
            error = errno;
            VLOG_ERR("failed to create raw socket (%s)", ovs_strerror(error));
            goto error;
        }

        /* Set non-blocking mode. */
        error = set_nonblocking(rx->fd);
        if (error) {
            goto error;
        }

        /* Get ethernet device index. */
        error = get_ifindex(&netdev->up, &ifindex);
        if (error) {
            goto error;
        }

        /* Bind to specific ethernet device. */
        memset(&sll, 0, sizeof sll);
        sll.sll_family = AF_PACKET;
        sll.sll_ifindex = ifindex;
        sll.sll_protocol = (OVS_FORCE unsigned short int) htons(ETH_P_ALL);
        if (bind(rx->fd, (struct sockaddr *) &sll, sizeof sll) < 0) {
            error = errno;
            VLOG_ERR("%s: failed to bind raw socket (%s)",
                     netdev_get_name(netdev_), ovs_strerror(error));
            goto error;
        }

        /* Filter for only inbound packets. */
        error = setsockopt(rx->fd, SOL_SOCKET, SO_ATTACH_FILTER, &fprog,
                           sizeof fprog);
        if (error) {
            error = errno;
            VLOG_ERR("%s: failed to attach filter (%s)",
                     netdev_get_name(netdev_), ovs_strerror(error));
            goto error;
        }
    }
    ovs_mutex_unlock(&netdev->mutex);

    return 0;

error:
    if (rx->fd >= 0) {
        close(rx->fd);
    }
    ovs_mutex_unlock(&netdev->mutex);
    return error;
}

static void
netdev_linux_rx_destruct(struct netdev_rx *rx_)
{
    struct netdev_rx_linux *rx = netdev_rx_linux_cast(rx_);

    if (!rx->is_tap) {
        close(rx->fd);
    }
}

static void
netdev_linux_rx_dealloc(struct netdev_rx *rx_)
{
    struct netdev_rx_linux *rx = netdev_rx_linux_cast(rx_);

    free(rx);
}

static int
netdev_linux_rx_recv(struct netdev_rx *rx_, void *data, size_t size)
{
    struct netdev_rx_linux *rx = netdev_rx_linux_cast(rx_);
    ssize_t retval;

    do {
        retval = (rx->is_tap
                  ? read(rx->fd, data, size)
                  : recv(rx->fd, data, size, MSG_TRUNC));
    } while (retval < 0 && errno == EINTR);

    if (retval >= 0) {
        return retval > size ? -EMSGSIZE : retval;
    } else {
        if (errno != EAGAIN) {
            VLOG_WARN_RL(&rl, "error receiving Ethernet packet on %s: %s",
                         ovs_strerror(errno), netdev_rx_get_name(rx_));
        }
        return -errno;
    }
}

static void
netdev_linux_rx_wait(struct netdev_rx *rx_)
{
    struct netdev_rx_linux *rx = netdev_rx_linux_cast(rx_);
    poll_fd_wait(rx->fd, POLLIN);
}

static int
netdev_linux_rx_drain(struct netdev_rx *rx_)
{
    struct netdev_rx_linux *rx = netdev_rx_linux_cast(rx_);
    if (rx->is_tap) {
        struct ifreq ifr;
        int error = af_inet_ifreq_ioctl(netdev_rx_get_name(rx_), &ifr,
                                        SIOCGIFTXQLEN, "SIOCGIFTXQLEN");
        if (error) {
            return error;
        }
        drain_fd(rx->fd, ifr.ifr_qlen);
        return 0;
    } else {
        return drain_rcvbuf(rx->fd);
    }
}

/* Sends 'buffer' on 'netdev'.  Returns 0 if successful, otherwise a positive
 * errno value.  Returns EAGAIN without blocking if the packet cannot be queued
 * immediately.  Returns EMSGSIZE if a partial packet was transmitted or if
 * the packet is too big or too small to transmit on the device.
 *
 * The caller retains ownership of 'buffer' in all cases.
 *
 * The kernel maintains a packet transmission queue, so the caller is not
 * expected to do additional queuing of packets. */
static int
netdev_linux_send(struct netdev *netdev_, const void *data, size_t size)
{
    for (;;) {
        ssize_t retval;

        if (!is_tap_netdev(netdev_)) {
            /* Use our AF_PACKET socket to send to this device. */
            struct sockaddr_ll sll;
            struct msghdr msg;
            struct iovec iov;
            int ifindex;
            int sock;

            sock = af_packet_sock();
            if (sock < 0) {
                return -sock;
            }

            ifindex = netdev_get_ifindex(netdev_);
            if (ifindex < 0) {
                return -ifindex;
            }

            /* We don't bother setting most fields in sockaddr_ll because the
             * kernel ignores them for SOCK_RAW. */
            memset(&sll, 0, sizeof sll);
            sll.sll_family = AF_PACKET;
            sll.sll_ifindex = ifindex;

            iov.iov_base = CONST_CAST(void *, data);
            iov.iov_len = size;

            msg.msg_name = &sll;
            msg.msg_namelen = sizeof sll;
            msg.msg_iov = &iov;
            msg.msg_iovlen = 1;
            msg.msg_control = NULL;
            msg.msg_controllen = 0;
            msg.msg_flags = 0;

            retval = sendmsg(sock, &msg, 0);
        } else {
            /* Use the tap fd to send to this device.  This is essential for
             * tap devices, because packets sent to a tap device with an
             * AF_PACKET socket will loop back to be *received* again on the
             * tap device.  This doesn't occur on other interface types
             * because we attach a socket filter to the rx socket. */
            struct netdev_linux *netdev = netdev_linux_cast(netdev_);

            retval = write(netdev->tap_fd, data, size);
        }

        if (retval < 0) {
            /* The Linux AF_PACKET implementation never blocks waiting for room
             * for packets, instead returning ENOBUFS.  Translate this into
             * EAGAIN for the caller. */
            if (errno == ENOBUFS) {
                return EAGAIN;
            } else if (errno == EINTR) {
                continue;
            } else if (errno != EAGAIN) {
                VLOG_WARN_RL(&rl, "error sending Ethernet packet on %s: %s",
                             netdev_get_name(netdev_), ovs_strerror(errno));
            }
            return errno;
        } else if (retval != size) {
            VLOG_WARN_RL(&rl, "sent partial Ethernet packet (%zd bytes of "
                         "%zu) on %s", retval, size, netdev_get_name(netdev_));
            return EMSGSIZE;
        } else {
            return 0;
        }
    }
}

/* Registers with the poll loop to wake up from the next call to poll_block()
 * when the packet transmission queue has sufficient room to transmit a packet
 * with netdev_send().
 *
 * The kernel maintains a packet transmission queue, so the client is not
 * expected to do additional queuing of packets.  Thus, this function is
 * unlikely to ever be used.  It is included for completeness. */
static void
netdev_linux_send_wait(struct netdev *netdev)
{
    if (is_tap_netdev(netdev)) {
        /* TAP device always accepts packets.*/
        poll_immediate_wake();
    }
}

/* Attempts to set 'netdev''s MAC address to 'mac'.  Returns 0 if successful,
 * otherwise a positive errno value. */
static int
netdev_linux_set_etheraddr(struct netdev *netdev_,
                           const uint8_t mac[ETH_ADDR_LEN])
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    enum netdev_flags old_flags = 0;
    int error;

    ovs_mutex_lock(&netdev->mutex);

    if (netdev->cache_valid & VALID_ETHERADDR) {
        error = netdev->ether_addr_error;
        if (error || eth_addr_equals(netdev->etheraddr, mac)) {
            goto exit;
        }
        netdev->cache_valid &= ~VALID_ETHERADDR;
    }

    /* Tap devices must be brought down before setting the address. */
    if (is_tap_netdev(netdev_)) {
        update_flags(netdev, NETDEV_UP, 0, &old_flags);
    }
    error = set_etheraddr(netdev_get_name(netdev_), mac);
    if (!error || error == ENODEV) {
        netdev->ether_addr_error = error;
        netdev->cache_valid |= VALID_ETHERADDR;
        if (!error) {
            memcpy(netdev->etheraddr, mac, ETH_ADDR_LEN);
        }
    }

    if (is_tap_netdev(netdev_) && old_flags & NETDEV_UP) {
        update_flags(netdev, 0, NETDEV_UP, &old_flags);
    }

exit:
    ovs_mutex_unlock(&netdev->mutex);
    return error;
}

/* Copies 'netdev''s MAC address to 'mac' which is passed as param. */
static int
netdev_linux_get_etheraddr(const struct netdev *netdev_,
                           uint8_t mac[ETH_ADDR_LEN])
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    int error;

    ovs_mutex_lock(&netdev->mutex);
    if (!(netdev->cache_valid & VALID_ETHERADDR)) {
        netdev->ether_addr_error = get_etheraddr(netdev_get_name(netdev_),
                                                 netdev->etheraddr);
        netdev->cache_valid |= VALID_ETHERADDR;
    }

    error = netdev->ether_addr_error;
    if (!error) {
        memcpy(mac, netdev->etheraddr, ETH_ADDR_LEN);
    }
    ovs_mutex_unlock(&netdev->mutex);

    return error;
}

static int
netdev_linux_get_mtu__(struct netdev_linux *netdev, int *mtup)
{
    int error;

    if (!(netdev->cache_valid & VALID_MTU)) {
        struct ifreq ifr;

        netdev->netdev_mtu_error = af_inet_ifreq_ioctl(
            netdev_get_name(&netdev->up), &ifr, SIOCGIFMTU, "SIOCGIFMTU");
        netdev->mtu = ifr.ifr_mtu;
        netdev->cache_valid |= VALID_MTU;
    }

    error = netdev->netdev_mtu_error;
    if (!error) {
        *mtup = netdev->mtu;
    }

    return error;
}

/* Returns the maximum size of transmitted (and received) packets on 'netdev',
 * in bytes, not including the hardware header; thus, this is typically 1500
 * bytes for Ethernet devices. */
static int
netdev_linux_get_mtu(const struct netdev *netdev_, int *mtup)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    int error;

    ovs_mutex_lock(&netdev->mutex);
    error = netdev_linux_get_mtu__(netdev, mtup);
    ovs_mutex_unlock(&netdev->mutex);

    return error;
}

/* Sets the maximum size of transmitted (MTU) for given device using linux
 * networking ioctl interface.
 */
static int
netdev_linux_set_mtu(const struct netdev *netdev_, int mtu)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    struct ifreq ifr;
    int error;

    ovs_mutex_lock(&netdev->mutex);
    if (netdev->cache_valid & VALID_MTU) {
        error = netdev->netdev_mtu_error;
        if (error || netdev->mtu == mtu) {
            goto exit;
        }
        netdev->cache_valid &= ~VALID_MTU;
    }
    ifr.ifr_mtu = mtu;
    error = af_inet_ifreq_ioctl(netdev_get_name(netdev_), &ifr,
                                SIOCSIFMTU, "SIOCSIFMTU");
    if (!error || error == ENODEV) {
        netdev->netdev_mtu_error = error;
        netdev->mtu = ifr.ifr_mtu;
        netdev->cache_valid |= VALID_MTU;
    }
exit:
    ovs_mutex_unlock(&netdev->mutex);
    return error;
}

/* Returns the ifindex of 'netdev', if successful, as a positive number.
 * On failure, returns a negative errno value. */
static int
netdev_linux_get_ifindex(const struct netdev *netdev_)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    int ifindex, error;

    ovs_mutex_lock(&netdev->mutex);
    error = get_ifindex(netdev_, &ifindex);
    ovs_mutex_unlock(&netdev->mutex);

    return error ? -error : ifindex;
}

static int
netdev_linux_get_carrier(const struct netdev *netdev_, bool *carrier)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);

    ovs_mutex_lock(&netdev->mutex);
    if (netdev->miimon_interval > 0) {
        *carrier = netdev->miimon;
    } else {
        *carrier = (netdev->ifi_flags & IFF_RUNNING) != 0;
    }
    ovs_mutex_unlock(&netdev->mutex);

    return 0;
}

static long long int
netdev_linux_get_carrier_resets(const struct netdev *netdev_)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    long long int carrier_resets;

    ovs_mutex_lock(&netdev->mutex);
    carrier_resets = netdev->carrier_resets;
    ovs_mutex_unlock(&netdev->mutex);

    return carrier_resets;
}

static int
netdev_linux_do_miimon(const char *name, int cmd, const char *cmd_name,
                       struct mii_ioctl_data *data)
{
    struct ifreq ifr;
    int error;

    memset(&ifr, 0, sizeof ifr);
    memcpy(&ifr.ifr_data, data, sizeof *data);
    error = af_inet_ifreq_ioctl(name, &ifr, cmd, cmd_name);
    memcpy(data, &ifr.ifr_data, sizeof *data);

    return error;
}

static int
netdev_linux_get_miimon(const char *name, bool *miimon)
{
    struct mii_ioctl_data data;
    int error;

    *miimon = false;

    memset(&data, 0, sizeof data);
    error = netdev_linux_do_miimon(name, SIOCGMIIPHY, "SIOCGMIIPHY", &data);
    if (!error) {
        /* data.phy_id is filled out by previous SIOCGMIIPHY miimon call. */
        data.reg_num = MII_BMSR;
        error = netdev_linux_do_miimon(name, SIOCGMIIREG, "SIOCGMIIREG",
                                       &data);

        if (!error) {
            *miimon = !!(data.val_out & BMSR_LSTATUS);
        } else {
            VLOG_WARN_RL(&rl, "%s: failed to query MII", name);
        }
    } else {
        struct ethtool_cmd ecmd;

        VLOG_DBG_RL(&rl, "%s: failed to query MII, falling back to ethtool",
                    name);

        COVERAGE_INC(netdev_get_ethtool);
        memset(&ecmd, 0, sizeof ecmd);
        error = netdev_linux_do_ethtool(name, &ecmd, ETHTOOL_GLINK,
                                        "ETHTOOL_GLINK");
        if (!error) {
            struct ethtool_value eval;

            memcpy(&eval, &ecmd, sizeof eval);
            *miimon = !!eval.data;
        } else {
            VLOG_WARN_RL(&rl, "%s: ethtool link status failed", name);
        }
    }

    return error;
}

static int
netdev_linux_set_miimon_interval(struct netdev *netdev_,
                                 long long int interval)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);

    ovs_mutex_lock(&netdev->mutex);
    interval = interval > 0 ? MAX(interval, 100) : 0;
    if (netdev->miimon_interval != interval) {
        netdev->miimon_interval = interval;
        timer_set_expired(&netdev->miimon_timer);
    }
    ovs_mutex_unlock(&netdev->mutex);

    return 0;
}

static void
netdev_linux_miimon_run(void)
{
    struct shash device_shash;
    struct shash_node *node;

    shash_init(&device_shash);
    netdev_get_devices(&netdev_linux_class, &device_shash);
    SHASH_FOR_EACH (node, &device_shash) {
        struct netdev *netdev = node->data;
        struct netdev_linux *dev = netdev_linux_cast(netdev);
        bool miimon;

        ovs_mutex_lock(&dev->mutex);
        if (dev->miimon_interval > 0 && timer_expired(&dev->miimon_timer)) {
            netdev_linux_get_miimon(dev->up.name, &miimon);
            if (miimon != dev->miimon) {
                dev->miimon = miimon;
                netdev_linux_changed(dev, dev->ifi_flags, 0);
            }

            timer_set_duration(&dev->miimon_timer, dev->miimon_interval);
        }
        ovs_mutex_unlock(&dev->mutex);
        netdev_close(netdev);
    }

    shash_destroy(&device_shash);
}

static void
netdev_linux_miimon_wait(void)
{
    struct shash device_shash;
    struct shash_node *node;

    shash_init(&device_shash);
    netdev_get_devices(&netdev_linux_class, &device_shash);
    SHASH_FOR_EACH (node, &device_shash) {
        struct netdev *netdev = node->data;
        struct netdev_linux *dev = netdev_linux_cast(netdev);

        ovs_mutex_lock(&dev->mutex);
        if (dev->miimon_interval > 0) {
            timer_wait(&dev->miimon_timer);
        }
        ovs_mutex_unlock(&dev->mutex);
        netdev_close(netdev);
    }
    shash_destroy(&device_shash);
}

/* Check whether we can we use RTM_GETLINK to get network device statistics.
 * In pre-2.6.19 kernels, this was only available if wireless extensions were
 * enabled. */
static bool
check_for_working_netlink_stats(void)
{
    /* Decide on the netdev_get_stats() implementation to use.  Netlink is
     * preferable, so if that works, we'll use it. */
    int ifindex = do_get_ifindex("lo");
    if (ifindex < 0) {
        VLOG_WARN("failed to get ifindex for lo, "
                  "obtaining netdev stats from proc");
        return false;
    } else {
        struct netdev_stats stats;
        int error = get_stats_via_netlink(ifindex, &stats);
        if (!error) {
            VLOG_DBG("obtaining netdev stats via rtnetlink");
            return true;
        } else {
            VLOG_INFO("RTM_GETLINK failed (%s), obtaining netdev stats "
                      "via proc (you are probably running a pre-2.6.19 "
                      "kernel)", ovs_strerror(error));
            return false;
        }
    }
}

static void
swap_uint64(uint64_t *a, uint64_t *b)
{
    uint64_t tmp = *a;
    *a = *b;
    *b = tmp;
}

/* Copies 'src' into 'dst', performing format conversion in the process.
 *
 * 'src' is allowed to be misaligned. */
static void
netdev_stats_from_ovs_vport_stats(struct netdev_stats *dst,
                                  const struct ovs_vport_stats *src)
{
    dst->rx_packets = get_unaligned_u64(&src->rx_packets);
    dst->tx_packets = get_unaligned_u64(&src->tx_packets);
    dst->rx_bytes = get_unaligned_u64(&src->rx_bytes);
    dst->tx_bytes = get_unaligned_u64(&src->tx_bytes);
    dst->rx_errors = get_unaligned_u64(&src->rx_errors);
    dst->tx_errors = get_unaligned_u64(&src->tx_errors);
    dst->rx_dropped = get_unaligned_u64(&src->rx_dropped);
    dst->tx_dropped = get_unaligned_u64(&src->tx_dropped);
    dst->multicast = 0;
    dst->collisions = 0;
    dst->rx_length_errors = 0;
    dst->rx_over_errors = 0;
    dst->rx_crc_errors = 0;
    dst->rx_frame_errors = 0;
    dst->rx_fifo_errors = 0;
    dst->rx_missed_errors = 0;
    dst->tx_aborted_errors = 0;
    dst->tx_carrier_errors = 0;
    dst->tx_fifo_errors = 0;
    dst->tx_heartbeat_errors = 0;
    dst->tx_window_errors = 0;
}

static int
get_stats_via_vport__(const struct netdev *netdev, struct netdev_stats *stats)
{
    struct dpif_linux_vport reply;
    struct ofpbuf *buf;
    int error;

    error = dpif_linux_vport_get(netdev_get_name(netdev), &reply, &buf);
    if (error) {
        return error;
    } else if (!reply.stats) {
        ofpbuf_delete(buf);
        return EOPNOTSUPP;
    }

    netdev_stats_from_ovs_vport_stats(stats, reply.stats);

    ofpbuf_delete(buf);

    return 0;
}

static void
get_stats_via_vport(const struct netdev *netdev_,
                    struct netdev_stats *stats)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);

    if (!netdev->vport_stats_error ||
        !(netdev->cache_valid & VALID_VPORT_STAT_ERROR)) {
        int error;

        error = get_stats_via_vport__(netdev_, stats);
        if (error && error != ENOENT) {
            VLOG_WARN_RL(&rl, "%s: obtaining netdev stats via vport failed "
                         "(%s)",
                         netdev_get_name(netdev_), ovs_strerror(error));
        }
        netdev->vport_stats_error = error;
        netdev->cache_valid |= VALID_VPORT_STAT_ERROR;
    }
}

static int
netdev_linux_sys_get_stats(const struct netdev *netdev_,
                           struct netdev_stats *stats)
{
    static struct ovsthread_once once = OVSTHREAD_ONCE_INITIALIZER;
    static int use_netlink_stats;
    int error;

    if (ovsthread_once_start(&once)) {
        use_netlink_stats = check_for_working_netlink_stats();
        ovsthread_once_done(&once);
    }

    if (use_netlink_stats) {
        int ifindex;

        error = get_ifindex(netdev_, &ifindex);
        if (!error) {
            error = get_stats_via_netlink(ifindex, stats);
        }
    } else {
        error = get_stats_via_proc(netdev_get_name(netdev_), stats);
    }

    if (error) {
        VLOG_WARN_RL(&rl, "%s: linux-sys get stats failed %d",
                      netdev_get_name(netdev_), error);
    }
    return error;

}

/* Retrieves current device stats for 'netdev-linux'. */
static int
netdev_linux_get_stats(const struct netdev *netdev_,
                       struct netdev_stats *stats)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    struct netdev_stats dev_stats;
    int error;

    ovs_mutex_lock(&netdev->mutex);
    get_stats_via_vport(netdev_, stats);
    error = netdev_linux_sys_get_stats(netdev_, &dev_stats);
    if (error) {
        if (!netdev->vport_stats_error) {
            error = 0;
        }
    } else if (netdev->vport_stats_error) {
        /* stats not available from OVS then use ioctl stats. */
        *stats = dev_stats;
    } else {
        stats->rx_errors           += dev_stats.rx_errors;
        stats->tx_errors           += dev_stats.tx_errors;
        stats->rx_dropped          += dev_stats.rx_dropped;
        stats->tx_dropped          += dev_stats.tx_dropped;
        stats->multicast           += dev_stats.multicast;
        stats->collisions          += dev_stats.collisions;
        stats->rx_length_errors    += dev_stats.rx_length_errors;
        stats->rx_over_errors      += dev_stats.rx_over_errors;
        stats->rx_crc_errors       += dev_stats.rx_crc_errors;
        stats->rx_frame_errors     += dev_stats.rx_frame_errors;
        stats->rx_fifo_errors      += dev_stats.rx_fifo_errors;
        stats->rx_missed_errors    += dev_stats.rx_missed_errors;
        stats->tx_aborted_errors   += dev_stats.tx_aborted_errors;
        stats->tx_carrier_errors   += dev_stats.tx_carrier_errors;
        stats->tx_fifo_errors      += dev_stats.tx_fifo_errors;
        stats->tx_heartbeat_errors += dev_stats.tx_heartbeat_errors;
        stats->tx_window_errors    += dev_stats.tx_window_errors;
    }
    ovs_mutex_unlock(&netdev->mutex);

    return error;
}

/* Retrieves current device stats for 'netdev-tap' netdev or
 * netdev-internal. */
static int
netdev_tap_get_stats(const struct netdev *netdev_, struct netdev_stats *stats)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    struct netdev_stats dev_stats;
    int error;

    ovs_mutex_lock(&netdev->mutex);
    get_stats_via_vport(netdev_, stats);
    error = netdev_linux_sys_get_stats(netdev_, &dev_stats);
    if (error) {
        if (!netdev->vport_stats_error) {
            error = 0;
        }
    } else if (netdev->vport_stats_error) {
        /* Transmit and receive stats will appear to be swapped relative to the
         * other ports since we are the one sending the data, not a remote
         * computer.  For consistency, we swap them back here. This does not
         * apply if we are getting stats from the vport layer because it always
         * tracks stats from the perspective of the switch. */

        *stats = dev_stats;
        swap_uint64(&stats->rx_packets, &stats->tx_packets);
        swap_uint64(&stats->rx_bytes, &stats->tx_bytes);
        swap_uint64(&stats->rx_errors, &stats->tx_errors);
        swap_uint64(&stats->rx_dropped, &stats->tx_dropped);
        stats->rx_length_errors = 0;
        stats->rx_over_errors = 0;
        stats->rx_crc_errors = 0;
        stats->rx_frame_errors = 0;
        stats->rx_fifo_errors = 0;
        stats->rx_missed_errors = 0;
        stats->tx_aborted_errors = 0;
        stats->tx_carrier_errors = 0;
        stats->tx_fifo_errors = 0;
        stats->tx_heartbeat_errors = 0;
        stats->tx_window_errors = 0;
    } else {
        stats->rx_dropped          += dev_stats.tx_dropped;
        stats->tx_dropped          += dev_stats.rx_dropped;

        stats->rx_errors           += dev_stats.tx_errors;
        stats->tx_errors           += dev_stats.rx_errors;

        stats->multicast           += dev_stats.multicast;
        stats->collisions          += dev_stats.collisions;
    }
    ovs_mutex_unlock(&netdev->mutex);

    return error;
}

static int
netdev_internal_get_stats(const struct netdev *netdev_,
                          struct netdev_stats *stats)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    int error;

    ovs_mutex_lock(&netdev->mutex);
    get_stats_via_vport(netdev_, stats);
    error = netdev->vport_stats_error;
    ovs_mutex_unlock(&netdev->mutex);

    return error;
}

static int
netdev_internal_set_stats(struct netdev *netdev,
                          const struct netdev_stats *stats)
{
    struct ovs_vport_stats vport_stats;
    struct dpif_linux_vport vport;
    int err;

    vport_stats.rx_packets = stats->rx_packets;
    vport_stats.tx_packets = stats->tx_packets;
    vport_stats.rx_bytes = stats->rx_bytes;
    vport_stats.tx_bytes = stats->tx_bytes;
    vport_stats.rx_errors = stats->rx_errors;
    vport_stats.tx_errors = stats->tx_errors;
    vport_stats.rx_dropped = stats->rx_dropped;
    vport_stats.tx_dropped = stats->tx_dropped;

    dpif_linux_vport_init(&vport);
    vport.cmd = OVS_VPORT_CMD_SET;
    vport.name = netdev_get_name(netdev);
    vport.stats = &vport_stats;

    err = dpif_linux_vport_transact(&vport, NULL, NULL);

    /* If the vport layer doesn't know about the device, that doesn't mean it
     * doesn't exist (after all were able to open it when netdev_open() was
     * called), it just means that it isn't attached and we'll be getting
     * stats a different way. */
    if (err == ENODEV) {
        err = EOPNOTSUPP;
    }

    return err;
}

static void
netdev_linux_read_features(struct netdev_linux *netdev)
{
    struct ethtool_cmd ecmd;
    uint32_t speed;
    int error;

    if (netdev->cache_valid & VALID_FEATURES) {
        return;
    }

    COVERAGE_INC(netdev_get_ethtool);
    memset(&ecmd, 0, sizeof ecmd);
    error = netdev_linux_do_ethtool(netdev->up.name, &ecmd,
                                    ETHTOOL_GSET, "ETHTOOL_GSET");
    if (error) {
        goto out;
    }

    /* Supported features. */
    netdev->supported = 0;
    if (ecmd.supported & SUPPORTED_10baseT_Half) {
        netdev->supported |= NETDEV_F_10MB_HD;
    }
    if (ecmd.supported & SUPPORTED_10baseT_Full) {
        netdev->supported |= NETDEV_F_10MB_FD;
    }
    if (ecmd.supported & SUPPORTED_100baseT_Half)  {
        netdev->supported |= NETDEV_F_100MB_HD;
    }
    if (ecmd.supported & SUPPORTED_100baseT_Full) {
        netdev->supported |= NETDEV_F_100MB_FD;
    }
    if (ecmd.supported & SUPPORTED_1000baseT_Half) {
        netdev->supported |= NETDEV_F_1GB_HD;
    }
    if (ecmd.supported & SUPPORTED_1000baseT_Full) {
        netdev->supported |= NETDEV_F_1GB_FD;
    }
    if (ecmd.supported & SUPPORTED_10000baseT_Full) {
        netdev->supported |= NETDEV_F_10GB_FD;
    }
    if (ecmd.supported & SUPPORTED_TP) {
        netdev->supported |= NETDEV_F_COPPER;
    }
    if (ecmd.supported & SUPPORTED_FIBRE) {
        netdev->supported |= NETDEV_F_FIBER;
    }
    if (ecmd.supported & SUPPORTED_Autoneg) {
        netdev->supported |= NETDEV_F_AUTONEG;
    }
    if (ecmd.supported & SUPPORTED_Pause) {
        netdev->supported |= NETDEV_F_PAUSE;
    }
    if (ecmd.supported & SUPPORTED_Asym_Pause) {
        netdev->supported |= NETDEV_F_PAUSE_ASYM;
    }

    /* Advertised features. */
    netdev->advertised = 0;
    if (ecmd.advertising & ADVERTISED_10baseT_Half) {
        netdev->advertised |= NETDEV_F_10MB_HD;
    }
    if (ecmd.advertising & ADVERTISED_10baseT_Full) {
        netdev->advertised |= NETDEV_F_10MB_FD;
    }
    if (ecmd.advertising & ADVERTISED_100baseT_Half) {
        netdev->advertised |= NETDEV_F_100MB_HD;
    }
    if (ecmd.advertising & ADVERTISED_100baseT_Full) {
        netdev->advertised |= NETDEV_F_100MB_FD;
    }
    if (ecmd.advertising & ADVERTISED_1000baseT_Half) {
        netdev->advertised |= NETDEV_F_1GB_HD;
    }
    if (ecmd.advertising & ADVERTISED_1000baseT_Full) {
        netdev->advertised |= NETDEV_F_1GB_FD;
    }
    if (ecmd.advertising & ADVERTISED_10000baseT_Full) {
        netdev->advertised |= NETDEV_F_10GB_FD;
    }
    if (ecmd.advertising & ADVERTISED_TP) {
        netdev->advertised |= NETDEV_F_COPPER;
    }
    if (ecmd.advertising & ADVERTISED_FIBRE) {
        netdev->advertised |= NETDEV_F_FIBER;
    }
    if (ecmd.advertising & ADVERTISED_Autoneg) {
        netdev->advertised |= NETDEV_F_AUTONEG;
    }
    if (ecmd.advertising & ADVERTISED_Pause) {
        netdev->advertised |= NETDEV_F_PAUSE;
    }
    if (ecmd.advertising & ADVERTISED_Asym_Pause) {
        netdev->advertised |= NETDEV_F_PAUSE_ASYM;
    }

    /* Current settings. */
    speed = ecmd.speed;
    if (speed == SPEED_10) {
        netdev->current = ecmd.duplex ? NETDEV_F_10MB_FD : NETDEV_F_10MB_HD;
    } else if (speed == SPEED_100) {
        netdev->current = ecmd.duplex ? NETDEV_F_100MB_FD : NETDEV_F_100MB_HD;
    } else if (speed == SPEED_1000) {
        netdev->current = ecmd.duplex ? NETDEV_F_1GB_FD : NETDEV_F_1GB_HD;
    } else if (speed == SPEED_10000) {
        netdev->current = NETDEV_F_10GB_FD;
    } else if (speed == 40000) {
        netdev->current = NETDEV_F_40GB_FD;
    } else if (speed == 100000) {
        netdev->current = NETDEV_F_100GB_FD;
    } else if (speed == 1000000) {
        netdev->current = NETDEV_F_1TB_FD;
    } else {
        netdev->current = 0;
    }

    if (ecmd.port == PORT_TP) {
        netdev->current |= NETDEV_F_COPPER;
    } else if (ecmd.port == PORT_FIBRE) {
        netdev->current |= NETDEV_F_FIBER;
    }

    if (ecmd.autoneg) {
        netdev->current |= NETDEV_F_AUTONEG;
    }

out:
    netdev->cache_valid |= VALID_FEATURES;
    netdev->get_features_error = error;
}

/* Stores the features supported by 'netdev' into of '*current', '*advertised',
 * '*supported', and '*peer'.  Each value is a bitmap of NETDEV_* bits.
 * Returns 0 if successful, otherwise a positive errno value. */
static int
netdev_linux_get_features(const struct netdev *netdev_,
                          enum netdev_features *current,
                          enum netdev_features *advertised,
                          enum netdev_features *supported,
                          enum netdev_features *peer)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    int error;

    ovs_mutex_lock(&netdev->mutex);
    netdev_linux_read_features(netdev);
    if (!netdev->get_features_error) {
        *current = netdev->current;
        *advertised = netdev->advertised;
        *supported = netdev->supported;
        *peer = 0;              /* XXX */
    }
    error = netdev->get_features_error;
    ovs_mutex_unlock(&netdev->mutex);

    return error;
}

/* Set the features advertised by 'netdev' to 'advertise'. */
static int
netdev_linux_set_advertisements(struct netdev *netdev_,
                                enum netdev_features advertise)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    struct ethtool_cmd ecmd;
    int error;

    ovs_mutex_lock(&netdev->mutex);

    COVERAGE_INC(netdev_get_ethtool);
    memset(&ecmd, 0, sizeof ecmd);
    error = netdev_linux_do_ethtool(netdev_get_name(netdev_), &ecmd,
                                    ETHTOOL_GSET, "ETHTOOL_GSET");
    if (error) {
        goto exit;
    }

    ecmd.advertising = 0;
    if (advertise & NETDEV_F_10MB_HD) {
        ecmd.advertising |= ADVERTISED_10baseT_Half;
    }
    if (advertise & NETDEV_F_10MB_FD) {
        ecmd.advertising |= ADVERTISED_10baseT_Full;
    }
    if (advertise & NETDEV_F_100MB_HD) {
        ecmd.advertising |= ADVERTISED_100baseT_Half;
    }
    if (advertise & NETDEV_F_100MB_FD) {
        ecmd.advertising |= ADVERTISED_100baseT_Full;
    }
    if (advertise & NETDEV_F_1GB_HD) {
        ecmd.advertising |= ADVERTISED_1000baseT_Half;
    }
    if (advertise & NETDEV_F_1GB_FD) {
        ecmd.advertising |= ADVERTISED_1000baseT_Full;
    }
    if (advertise & NETDEV_F_10GB_FD) {
        ecmd.advertising |= ADVERTISED_10000baseT_Full;
    }
    if (advertise & NETDEV_F_COPPER) {
        ecmd.advertising |= ADVERTISED_TP;
    }
    if (advertise & NETDEV_F_FIBER) {
        ecmd.advertising |= ADVERTISED_FIBRE;
    }
    if (advertise & NETDEV_F_AUTONEG) {
        ecmd.advertising |= ADVERTISED_Autoneg;
    }
    if (advertise & NETDEV_F_PAUSE) {
        ecmd.advertising |= ADVERTISED_Pause;
    }
    if (advertise & NETDEV_F_PAUSE_ASYM) {
        ecmd.advertising |= ADVERTISED_Asym_Pause;
    }
    COVERAGE_INC(netdev_set_ethtool);
    error = netdev_linux_do_ethtool(netdev_get_name(netdev_), &ecmd,
                                    ETHTOOL_SSET, "ETHTOOL_SSET");

exit:
    ovs_mutex_unlock(&netdev->mutex);
    return error;
}

/* Attempts to set input rate limiting (policing) policy.  Returns 0 if
 * successful, otherwise a positive errno value. */
static int
netdev_linux_set_policing(struct netdev *netdev_,
                          uint32_t kbits_rate, uint32_t kbits_burst)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    const char *netdev_name = netdev_get_name(netdev_);
    int error;

    kbits_burst = (!kbits_rate ? 0       /* Force to 0 if no rate specified. */
                   : !kbits_burst ? 1000 /* Default to 1000 kbits if 0. */
                   : kbits_burst);       /* Stick with user-specified value. */

    ovs_mutex_lock(&netdev->mutex);
    if (netdev->cache_valid & VALID_POLICING) {
        error = netdev->netdev_policing_error;
        if (error || (netdev->kbits_rate == kbits_rate &&
                      netdev->kbits_burst == kbits_burst)) {
            /* Assume that settings haven't changed since we last set them. */
            goto out;
        }
        netdev->cache_valid &= ~VALID_POLICING;
    }

    COVERAGE_INC(netdev_set_policing);
    /* Remove any existing ingress qdisc. */
    error = tc_add_del_ingress_qdisc(netdev_, false);
    if (error) {
        VLOG_WARN_RL(&rl, "%s: removing policing failed: %s",
                     netdev_name, ovs_strerror(error));
        goto out;
    }

    if (kbits_rate) {
        error = tc_add_del_ingress_qdisc(netdev_, true);
        if (error) {
            VLOG_WARN_RL(&rl, "%s: adding policing qdisc failed: %s",
                         netdev_name, ovs_strerror(error));
            goto out;
        }

        error = tc_add_policer(netdev_, kbits_rate, kbits_burst);
        if (error){
            VLOG_WARN_RL(&rl, "%s: adding policing action failed: %s",
                    netdev_name, ovs_strerror(error));
            goto out;
        }
    }

    netdev->kbits_rate = kbits_rate;
    netdev->kbits_burst = kbits_burst;

out:
    if (!error || error == ENODEV) {
        netdev->netdev_policing_error = error;
        netdev->cache_valid |= VALID_POLICING;
    }
    ovs_mutex_unlock(&netdev->mutex);
    return error;
}

static int
netdev_linux_get_qos_types(const struct netdev *netdev OVS_UNUSED,
                           struct sset *types)
{
    const struct tc_ops *const *opsp;

    for (opsp = tcs; *opsp != NULL; opsp++) {
        const struct tc_ops *ops = *opsp;
        if (ops->tc_install && ops->ovs_name[0] != '\0') {
            sset_add(types, ops->ovs_name);
        }
    }
    return 0;
}

static const struct tc_ops *
tc_lookup_ovs_name(const char *name)
{
    const struct tc_ops *const *opsp;

    for (opsp = tcs; *opsp != NULL; opsp++) {
        const struct tc_ops *ops = *opsp;
        if (!strcmp(name, ops->ovs_name)) {
            return ops;
        }
    }
    return NULL;
}

static const struct tc_ops *
tc_lookup_linux_name(const char *name)
{
    const struct tc_ops *const *opsp;

    for (opsp = tcs; *opsp != NULL; opsp++) {
        const struct tc_ops *ops = *opsp;
        if (ops->linux_name && !strcmp(name, ops->linux_name)) {
            return ops;
        }
    }
    return NULL;
}

static struct tc_queue *
tc_find_queue__(const struct netdev *netdev_, unsigned int queue_id,
                size_t hash)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    struct tc_queue *queue;

    HMAP_FOR_EACH_IN_BUCKET (queue, hmap_node, hash, &netdev->tc->queues) {
        if (queue->queue_id == queue_id) {
            return queue;
        }
    }
    return NULL;
}

static struct tc_queue *
tc_find_queue(const struct netdev *netdev, unsigned int queue_id)
{
    return tc_find_queue__(netdev, queue_id, hash_int(queue_id, 0));
}

static int
netdev_linux_get_qos_capabilities(const struct netdev *netdev OVS_UNUSED,
                                  const char *type,
                                  struct netdev_qos_capabilities *caps)
{
    const struct tc_ops *ops = tc_lookup_ovs_name(type);
    if (!ops) {
        return EOPNOTSUPP;
    }
    caps->n_queues = ops->n_queues;
    return 0;
}

static int
netdev_linux_get_qos(const struct netdev *netdev_,
                     const char **typep, struct smap *details)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    int error;

    ovs_mutex_lock(&netdev->mutex);
    error = tc_query_qdisc(netdev_);
    if (!error) {
        *typep = netdev->tc->ops->ovs_name;
        error = (netdev->tc->ops->qdisc_get
                 ? netdev->tc->ops->qdisc_get(netdev_, details)
                 : 0);
    }
    ovs_mutex_unlock(&netdev->mutex);

    return error;
}

static int
netdev_linux_set_qos(struct netdev *netdev_,
                     const char *type, const struct smap *details)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    const struct tc_ops *new_ops;
    int error;

    new_ops = tc_lookup_ovs_name(type);
    if (!new_ops || !new_ops->tc_install) {
        return EOPNOTSUPP;
    }

    ovs_mutex_lock(&netdev->mutex);
    error = tc_query_qdisc(netdev_);
    if (error) {
        goto exit;
    }

    if (new_ops == netdev->tc->ops) {
        error = new_ops->qdisc_set ? new_ops->qdisc_set(netdev_, details) : 0;
    } else {
        /* Delete existing qdisc. */
        error = tc_del_qdisc(netdev_);
        if (error) {
            goto exit;
        }
        ovs_assert(netdev->tc == NULL);

        /* Install new qdisc. */
        error = new_ops->tc_install(netdev_, details);
        ovs_assert((error == 0) == (netdev->tc != NULL));
    }

exit:
    ovs_mutex_unlock(&netdev->mutex);
    return error;
}

static int
netdev_linux_get_queue(const struct netdev *netdev_,
                       unsigned int queue_id, struct smap *details)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    int error;

    ovs_mutex_lock(&netdev->mutex);
    error = tc_query_qdisc(netdev_);
    if (!error) {
        struct tc_queue *queue = tc_find_queue(netdev_, queue_id);
        error = (queue
                ? netdev->tc->ops->class_get(netdev_, queue, details)
                : ENOENT);
    }
    ovs_mutex_unlock(&netdev->mutex);

    return error;
}

static int
netdev_linux_set_queue(struct netdev *netdev_,
                       unsigned int queue_id, const struct smap *details)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    int error;

    ovs_mutex_lock(&netdev->mutex);
    error = tc_query_qdisc(netdev_);
    if (!error) {
        error = (queue_id < netdev->tc->ops->n_queues
                 && netdev->tc->ops->class_set
                 ? netdev->tc->ops->class_set(netdev_, queue_id, details)
                 : EINVAL);
    }
    ovs_mutex_unlock(&netdev->mutex);

    return error;
}

static int
netdev_linux_delete_queue(struct netdev *netdev_, unsigned int queue_id)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    int error;

    ovs_mutex_lock(&netdev->mutex);
    error = tc_query_qdisc(netdev_);
    if (!error) {
        if (netdev->tc->ops->class_delete) {
            struct tc_queue *queue = tc_find_queue(netdev_, queue_id);
            error = (queue
                     ? netdev->tc->ops->class_delete(netdev_, queue)
                     : ENOENT);
        } else {
            error = EINVAL;
        }
    }
    ovs_mutex_unlock(&netdev->mutex);

    return error;
}

static int
netdev_linux_get_queue_stats(const struct netdev *netdev_,
                             unsigned int queue_id,
                             struct netdev_queue_stats *stats)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    int error;

    ovs_mutex_lock(&netdev->mutex);
    error = tc_query_qdisc(netdev_);
    if (!error) {
        if (netdev->tc->ops->class_get_stats) {
            const struct tc_queue *queue = tc_find_queue(netdev_, queue_id);
            if (queue) {
                stats->created = queue->created;
                error = netdev->tc->ops->class_get_stats(netdev_, queue,
                                                         stats);
            } else {
                error = ENOENT;
            }
        } else {
            error = EOPNOTSUPP;
        }
    }
    ovs_mutex_unlock(&netdev->mutex);

    return error;
}

static bool
start_queue_dump(const struct netdev *netdev, struct nl_dump *dump)
{
    struct ofpbuf request;
    struct tcmsg *tcmsg;

    tcmsg = tc_make_request(netdev, RTM_GETTCLASS, 0, &request);
    if (!tcmsg) {
        return false;
    }
    tcmsg->tcm_parent = 0;
    nl_dump_start(dump, NETLINK_ROUTE, &request);
    ofpbuf_uninit(&request);
    return true;
}

struct netdev_linux_queue_state {
    unsigned int *queues;
    size_t cur_queue;
    size_t n_queues;
};

static int
netdev_linux_queue_dump_start(const struct netdev *netdev_, void **statep)
{
    const struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    int error;

    ovs_mutex_lock(&netdev->mutex);
    error = tc_query_qdisc(netdev_);
    if (!error) {
        if (netdev->tc->ops->class_get) {
            struct netdev_linux_queue_state *state;
            struct tc_queue *queue;
            size_t i;

            *statep = state = xmalloc(sizeof *state);
            state->n_queues = hmap_count(&netdev->tc->queues);
            state->cur_queue = 0;
            state->queues = xmalloc(state->n_queues * sizeof *state->queues);

            i = 0;
            HMAP_FOR_EACH (queue, hmap_node, &netdev->tc->queues) {
                state->queues[i++] = queue->queue_id;
            }
        } else {
            error = EOPNOTSUPP;
        }
    }
    ovs_mutex_unlock(&netdev->mutex);

    return error;
}

static int
netdev_linux_queue_dump_next(const struct netdev *netdev_, void *state_,
                             unsigned int *queue_idp, struct smap *details)
{
    const struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    struct netdev_linux_queue_state *state = state_;
    int error = EOF;

    ovs_mutex_lock(&netdev->mutex);
    while (state->cur_queue < state->n_queues) {
        unsigned int queue_id = state->queues[state->cur_queue++];
        struct tc_queue *queue = tc_find_queue(netdev_, queue_id);

        if (queue) {
            *queue_idp = queue_id;
            error = netdev->tc->ops->class_get(netdev_, queue, details);
            break;
        }
    }
    ovs_mutex_unlock(&netdev->mutex);

    return error;
}

static int
netdev_linux_queue_dump_done(const struct netdev *netdev OVS_UNUSED,
                             void *state_)
{
    struct netdev_linux_queue_state *state = state_;

    free(state->queues);
    free(state);
    return 0;
}

static int
netdev_linux_dump_queue_stats(const struct netdev *netdev_,
                              netdev_dump_queue_stats_cb *cb, void *aux)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    int error;

    ovs_mutex_lock(&netdev->mutex);
    error = tc_query_qdisc(netdev_);
    if (!error) {
        struct nl_dump dump;

        if (!netdev->tc->ops->class_dump_stats) {
            error = EOPNOTSUPP;
        } else if (!start_queue_dump(netdev_, &dump)) {
            error = ENODEV;
        } else {
            struct ofpbuf msg;
            int retval;

            while (nl_dump_next(&dump, &msg)) {
                retval = netdev->tc->ops->class_dump_stats(netdev_, &msg,
                                                           cb, aux);
                if (retval) {
                    error = retval;
                }
            }

            retval = nl_dump_done(&dump);
            if (retval) {
                error = retval;
            }
        }
    }
    ovs_mutex_unlock(&netdev->mutex);

    return error;
}

static int
netdev_linux_get_in4(const struct netdev *netdev_,
                     struct in_addr *address, struct in_addr *netmask)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    int error;

    ovs_mutex_lock(&netdev->mutex);
    if (!(netdev->cache_valid & VALID_IN4)) {
        error = netdev_linux_get_ipv4(netdev_, &netdev->address,
                                      SIOCGIFADDR, "SIOCGIFADDR");
        if (!error) {
            error = netdev_linux_get_ipv4(netdev_, &netdev->netmask,
                                          SIOCGIFNETMASK, "SIOCGIFNETMASK");
            if (!error) {
                netdev->cache_valid |= VALID_IN4;
            }
        }
    } else {
        error = 0;
    }

    if (!error) {
        if (netdev->address.s_addr != INADDR_ANY) {
            *address = netdev->address;
            *netmask = netdev->netmask;
        } else {
            error = EADDRNOTAVAIL;
        }
    }
    ovs_mutex_unlock(&netdev->mutex);

    return error;
}

static int
netdev_linux_set_in4(struct netdev *netdev_, struct in_addr address,
                     struct in_addr netmask)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    int error;

    ovs_mutex_lock(&netdev->mutex);
    error = do_set_addr(netdev_, SIOCSIFADDR, "SIOCSIFADDR", address);
    if (!error) {
        netdev->cache_valid |= VALID_IN4;
        netdev->address = address;
        netdev->netmask = netmask;
        if (address.s_addr != INADDR_ANY) {
            error = do_set_addr(netdev_, SIOCSIFNETMASK,
                                "SIOCSIFNETMASK", netmask);
        }
    }
    ovs_mutex_unlock(&netdev->mutex);

    return error;
}

static bool
parse_if_inet6_line(const char *line,
                    struct in6_addr *in6, char ifname[16 + 1])
{
    uint8_t *s6 = in6->s6_addr;
#define X8 "%2"SCNx8
    return sscanf(line,
                  " "X8 X8 X8 X8 X8 X8 X8 X8 X8 X8 X8 X8 X8 X8 X8 X8
                  "%*x %*x %*x %*x %16s\n",
                  &s6[0], &s6[1], &s6[2], &s6[3],
                  &s6[4], &s6[5], &s6[6], &s6[7],
                  &s6[8], &s6[9], &s6[10], &s6[11],
                  &s6[12], &s6[13], &s6[14], &s6[15],
                  ifname) == 17;
}

/* If 'netdev' has an assigned IPv6 address, sets '*in6' to that address (if
 * 'in6' is non-null) and returns true.  Otherwise, returns false. */
static int
netdev_linux_get_in6(const struct netdev *netdev_, struct in6_addr *in6)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);

    ovs_mutex_lock(&netdev->mutex);
    if (!(netdev->cache_valid & VALID_IN6)) {
        FILE *file;
        char line[128];

        netdev->in6 = in6addr_any;

        file = fopen("/proc/net/if_inet6", "r");
        if (file != NULL) {
            const char *name = netdev_get_name(netdev_);
            while (fgets(line, sizeof line, file)) {
                struct in6_addr in6_tmp;
                char ifname[16 + 1];
                if (parse_if_inet6_line(line, &in6_tmp, ifname)
                    && !strcmp(name, ifname))
                {
                    netdev->in6 = in6_tmp;
                    break;
                }
            }
            fclose(file);
        }
        netdev->cache_valid |= VALID_IN6;
    }
    *in6 = netdev->in6;
    ovs_mutex_unlock(&netdev->mutex);

    return 0;
}

static void
make_in4_sockaddr(struct sockaddr *sa, struct in_addr addr)
{
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof sin);
    sin.sin_family = AF_INET;
    sin.sin_addr = addr;
    sin.sin_port = 0;

    memset(sa, 0, sizeof *sa);
    memcpy(sa, &sin, sizeof sin);
}

static int
do_set_addr(struct netdev *netdev,
            int ioctl_nr, const char *ioctl_name, struct in_addr addr)
{
    struct ifreq ifr;

    make_in4_sockaddr(&ifr.ifr_addr, addr);
    return af_inet_ifreq_ioctl(netdev_get_name(netdev), &ifr, ioctl_nr,
                               ioctl_name);
}

/* Adds 'router' as a default IP gateway. */
static int
netdev_linux_add_router(struct netdev *netdev OVS_UNUSED, struct in_addr router)
{
    struct in_addr any = { INADDR_ANY };
    struct rtentry rt;
    int error;

    memset(&rt, 0, sizeof rt);
    make_in4_sockaddr(&rt.rt_dst, any);
    make_in4_sockaddr(&rt.rt_gateway, router);
    make_in4_sockaddr(&rt.rt_genmask, any);
    rt.rt_flags = RTF_UP | RTF_GATEWAY;
    error = af_inet_ioctl(SIOCADDRT, &rt);
    if (error) {
        VLOG_WARN("ioctl(SIOCADDRT): %s", ovs_strerror(error));
    }
    return error;
}

static int
netdev_linux_get_next_hop(const struct in_addr *host, struct in_addr *next_hop,
                          char **netdev_name)
{
    static const char fn[] = "/proc/net/route";
    FILE *stream;
    char line[256];
    int ln;

    *netdev_name = NULL;
    stream = fopen(fn, "r");
    if (stream == NULL) {
        VLOG_WARN_RL(&rl, "%s: open failed: %s", fn, ovs_strerror(errno));
        return errno;
    }

    ln = 0;
    while (fgets(line, sizeof line, stream)) {
        if (++ln >= 2) {
            char iface[17];
            ovs_be32 dest, gateway, mask;
            int refcnt, metric, mtu;
            unsigned int flags, use, window, irtt;

            if (sscanf(line,
                       "%16s %"SCNx32" %"SCNx32" %04X %d %u %d %"SCNx32
                       " %d %u %u\n",
                       iface, &dest, &gateway, &flags, &refcnt,
                       &use, &metric, &mask, &mtu, &window, &irtt) != 11) {

                VLOG_WARN_RL(&rl, "%s: could not parse line %d: %s",
                        fn, ln, line);
                continue;
            }
            if (!(flags & RTF_UP)) {
                /* Skip routes that aren't up. */
                continue;
            }

            /* The output of 'dest', 'mask', and 'gateway' were given in
             * network byte order, so we don't need need any endian
             * conversions here. */
            if ((dest & mask) == (host->s_addr & mask)) {
                if (!gateway) {
                    /* The host is directly reachable. */
                    next_hop->s_addr = 0;
                } else {
                    /* To reach the host, we must go through a gateway. */
                    next_hop->s_addr = gateway;
                }
                *netdev_name = xstrdup(iface);
                fclose(stream);
                return 0;
            }
        }
    }

    fclose(stream);
    return ENXIO;
}

static int
netdev_linux_get_status(const struct netdev *netdev_, struct smap *smap)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    int error = 0;

    ovs_mutex_lock(&netdev->mutex);
    if (!(netdev->cache_valid & VALID_DRVINFO)) {
        struct ethtool_cmd *cmd = (struct ethtool_cmd *) &netdev->drvinfo;

        COVERAGE_INC(netdev_get_ethtool);
        memset(&netdev->drvinfo, 0, sizeof netdev->drvinfo);
        error = netdev_linux_do_ethtool(netdev->up.name,
                                        cmd,
                                        ETHTOOL_GDRVINFO,
                                        "ETHTOOL_GDRVINFO");
        if (!error) {
            netdev->cache_valid |= VALID_DRVINFO;
        }
    }

    if (!error) {
        smap_add(smap, "driver_name", netdev->drvinfo.driver);
        smap_add(smap, "driver_version", netdev->drvinfo.version);
        smap_add(smap, "firmware_version", netdev->drvinfo.fw_version);
    }
    ovs_mutex_unlock(&netdev->mutex);

    return error;
}

static int
netdev_internal_get_status(const struct netdev *netdev OVS_UNUSED,
                           struct smap *smap)
{
    smap_add(smap, "driver_name", "openvswitch");
    return 0;
}

/* Looks up the ARP table entry for 'ip' on 'netdev'.  If one exists and can be
 * successfully retrieved, it stores the corresponding MAC address in 'mac' and
 * returns 0.  Otherwise, it returns a positive errno value; in particular,
 * ENXIO indicates that there is not ARP table entry for 'ip' on 'netdev'. */
static int
netdev_linux_arp_lookup(const struct netdev *netdev,
                        ovs_be32 ip, uint8_t mac[ETH_ADDR_LEN])
{
    struct arpreq r;
    struct sockaddr_in sin;
    int retval;

    memset(&r, 0, sizeof r);
    memset(&sin, 0, sizeof sin);
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = ip;
    sin.sin_port = 0;
    memcpy(&r.arp_pa, &sin, sizeof sin);
    r.arp_ha.sa_family = ARPHRD_ETHER;
    r.arp_flags = 0;
    ovs_strzcpy(r.arp_dev, netdev_get_name(netdev), sizeof r.arp_dev);
    COVERAGE_INC(netdev_arp_lookup);
    retval = af_inet_ioctl(SIOCGARP, &r);
    if (!retval) {
        memcpy(mac, r.arp_ha.sa_data, ETH_ADDR_LEN);
    } else if (retval != ENXIO) {
        VLOG_WARN_RL(&rl, "%s: could not look up ARP entry for "IP_FMT": %s",
                     netdev_get_name(netdev), IP_ARGS(ip),
                     ovs_strerror(retval));
    }
    return retval;
}

static int
nd_to_iff_flags(enum netdev_flags nd)
{
    int iff = 0;
    if (nd & NETDEV_UP) {
        iff |= IFF_UP;
    }
    if (nd & NETDEV_PROMISC) {
        iff |= IFF_PROMISC;
    }
    return iff;
}

static int
iff_to_nd_flags(int iff)
{
    enum netdev_flags nd = 0;
    if (iff & IFF_UP) {
        nd |= NETDEV_UP;
    }
    if (iff & IFF_PROMISC) {
        nd |= NETDEV_PROMISC;
    }
    return nd;
}

static int
update_flags(struct netdev_linux *netdev, enum netdev_flags off,
             enum netdev_flags on, enum netdev_flags *old_flagsp)
    OVS_REQUIRES(netdev->mutex)
{
    int old_flags, new_flags;
    int error = 0;

    old_flags = netdev->ifi_flags;
    *old_flagsp = iff_to_nd_flags(old_flags);
    new_flags = (old_flags & ~nd_to_iff_flags(off)) | nd_to_iff_flags(on);
    if (new_flags != old_flags) {
        error = set_flags(netdev_get_name(&netdev->up), new_flags);
        get_flags(&netdev->up, &netdev->ifi_flags);
    }

    return error;
}

static int
netdev_linux_update_flags(struct netdev *netdev_, enum netdev_flags off,
                          enum netdev_flags on, enum netdev_flags *old_flagsp)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    int error;

    ovs_mutex_lock(&netdev->mutex);
    error = update_flags(netdev, off, on, old_flagsp);
    ovs_mutex_unlock(&netdev->mutex);

    return error;
}

static unsigned int
netdev_linux_change_seq(const struct netdev *netdev_)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    unsigned int change_seq;

    ovs_mutex_lock(&netdev->mutex);
    change_seq = netdev->change_seq;
    ovs_mutex_unlock(&netdev->mutex);

    return change_seq;
}

#define NETDEV_LINUX_CLASS(NAME, CONSTRUCT, GET_STATS, SET_STATS,  \
                           GET_FEATURES, GET_STATUS)            \
{                                                               \
    NAME,                                                       \
                                                                \
    NULL,                                                       \
    netdev_linux_run,                                           \
    netdev_linux_wait,                                          \
                                                                \
    netdev_linux_alloc,                                         \
    CONSTRUCT,                                                  \
    netdev_linux_destruct,                                      \
    netdev_linux_dealloc,                                       \
    NULL,                       /* get_config */                \
    NULL,                       /* set_config */                \
    NULL,                       /* get_tunnel_config */         \
                                                                \
    netdev_linux_send,                                          \
    netdev_linux_send_wait,                                     \
                                                                \
    netdev_linux_set_etheraddr,                                 \
    netdev_linux_get_etheraddr,                                 \
    netdev_linux_get_mtu,                                       \
    netdev_linux_set_mtu,                                       \
    netdev_linux_get_ifindex,                                   \
    netdev_linux_get_carrier,                                   \
    netdev_linux_get_carrier_resets,                            \
    netdev_linux_set_miimon_interval,                           \
    GET_STATS,                                                  \
    SET_STATS,                                                  \
                                                                \
    GET_FEATURES,                                               \
    netdev_linux_set_advertisements,                            \
                                                                \
    netdev_linux_set_policing,                                  \
    netdev_linux_get_qos_types,                                 \
    netdev_linux_get_qos_capabilities,                          \
    netdev_linux_get_qos,                                       \
    netdev_linux_set_qos,                                       \
    netdev_linux_get_queue,                                     \
    netdev_linux_set_queue,                                     \
    netdev_linux_delete_queue,                                  \
    netdev_linux_get_queue_stats,                               \
    netdev_linux_queue_dump_start,                              \
    netdev_linux_queue_dump_next,                               \
    netdev_linux_queue_dump_done,                               \
    netdev_linux_dump_queue_stats,                              \
                                                                \
    netdev_linux_get_in4,                                       \
    netdev_linux_set_in4,                                       \
    netdev_linux_get_in6,                                       \
    netdev_linux_add_router,                                    \
    netdev_linux_get_next_hop,                                  \
    GET_STATUS,                                                 \
    netdev_linux_arp_lookup,                                    \
                                                                \
    netdev_linux_update_flags,                                  \
                                                                \
    netdev_linux_change_seq,                                    \
                                                                \
    netdev_linux_rx_alloc,                                      \
    netdev_linux_rx_construct,                                  \
    netdev_linux_rx_destruct,                                   \
    netdev_linux_rx_dealloc,                                    \
    netdev_linux_rx_recv,                                       \
    netdev_linux_rx_wait,                                       \
    netdev_linux_rx_drain,                                      \
}

const struct netdev_class netdev_linux_class =
    NETDEV_LINUX_CLASS(
        "system",
        netdev_linux_construct,
        netdev_linux_get_stats,
        NULL,                    /* set_stats */
        netdev_linux_get_features,
        netdev_linux_get_status);

const struct netdev_class netdev_tap_class =
    NETDEV_LINUX_CLASS(
        "tap",
        netdev_linux_construct_tap,
        netdev_tap_get_stats,
        NULL,                   /* set_stats */
        netdev_linux_get_features,
        netdev_linux_get_status);

const struct netdev_class netdev_internal_class =
    NETDEV_LINUX_CLASS(
        "internal",
        netdev_linux_construct,
        netdev_internal_get_stats,
        netdev_internal_set_stats,
        NULL,                  /* get_features */
        netdev_internal_get_status);

/* HTB traffic control class. */

#define HTB_N_QUEUES 0xf000

struct htb {
    struct tc tc;
    unsigned int max_rate;      /* In bytes/s. */
};

struct htb_class {
    struct tc_queue tc_queue;
    unsigned int min_rate;      /* In bytes/s. */
    unsigned int max_rate;      /* In bytes/s. */
    unsigned int burst;         /* In bytes. */
    unsigned int priority;      /* Lower values are higher priorities. */
};

static struct htb *
htb_get__(const struct netdev *netdev_)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    return CONTAINER_OF(netdev->tc, struct htb, tc);
}

static void
htb_install__(struct netdev *netdev_, uint64_t max_rate)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    struct htb *htb;

    htb = xmalloc(sizeof *htb);
    tc_init(&htb->tc, &tc_ops_htb);
    htb->max_rate = max_rate;

    netdev->tc = &htb->tc;
}

/* Create an HTB qdisc.
 *
 * Equivalent to "tc qdisc add dev <dev> root handle 1: htb default 1". */
static int
htb_setup_qdisc__(struct netdev *netdev)
{
    size_t opt_offset;
    struct tc_htb_glob opt;
    struct ofpbuf request;
    struct tcmsg *tcmsg;

    tc_del_qdisc(netdev);

    tcmsg = tc_make_request(netdev, RTM_NEWQDISC,
                            NLM_F_EXCL | NLM_F_CREATE, &request);
    if (!tcmsg) {
        return ENODEV;
    }
    tcmsg->tcm_handle = tc_make_handle(1, 0);
    tcmsg->tcm_parent = TC_H_ROOT;

    nl_msg_put_string(&request, TCA_KIND, "htb");

    memset(&opt, 0, sizeof opt);
    opt.rate2quantum = 10;
    opt.version = 3;
    opt.defcls = 1;

    opt_offset = nl_msg_start_nested(&request, TCA_OPTIONS);
    nl_msg_put_unspec(&request, TCA_HTB_INIT, &opt, sizeof opt);
    nl_msg_end_nested(&request, opt_offset);

    return tc_transact(&request, NULL);
}

/* Equivalent to "tc class replace <dev> classid <handle> parent <parent> htb
 * rate <min_rate>bps ceil <max_rate>bps burst <burst>b prio <priority>". */
static int
htb_setup_class__(struct netdev *netdev, unsigned int handle,
                  unsigned int parent, struct htb_class *class)
{
    size_t opt_offset;
    struct tc_htb_opt opt;
    struct ofpbuf request;
    struct tcmsg *tcmsg;
    int error;
    int mtu;

    error = netdev_linux_get_mtu__(netdev_linux_cast(netdev), &mtu);
    if (error) {
        VLOG_WARN_RL(&rl, "cannot set up HTB on device %s that lacks MTU",
                     netdev_get_name(netdev));
        return error;
    }

    memset(&opt, 0, sizeof opt);
    tc_fill_rate(&opt.rate, class->min_rate, mtu);
    tc_fill_rate(&opt.ceil, class->max_rate, mtu);
    opt.buffer = tc_calc_buffer(opt.rate.rate, mtu, class->burst);
    opt.cbuffer = tc_calc_buffer(opt.ceil.rate, mtu, class->burst);
    opt.prio = class->priority;

    tcmsg = tc_make_request(netdev, RTM_NEWTCLASS, NLM_F_CREATE, &request);
    if (!tcmsg) {
        return ENODEV;
    }
    tcmsg->tcm_handle = handle;
    tcmsg->tcm_parent = parent;

    nl_msg_put_string(&request, TCA_KIND, "htb");
    opt_offset = nl_msg_start_nested(&request, TCA_OPTIONS);
    nl_msg_put_unspec(&request, TCA_HTB_PARMS, &opt, sizeof opt);
    tc_put_rtab(&request, TCA_HTB_RTAB, &opt.rate);
    tc_put_rtab(&request, TCA_HTB_CTAB, &opt.ceil);
    nl_msg_end_nested(&request, opt_offset);

    error = tc_transact(&request, NULL);
    if (error) {
        VLOG_WARN_RL(&rl, "failed to replace %s class %u:%u, parent %u:%u, "
                     "min_rate=%u max_rate=%u burst=%u prio=%u (%s)",
                     netdev_get_name(netdev),
                     tc_get_major(handle), tc_get_minor(handle),
                     tc_get_major(parent), tc_get_minor(parent),
                     class->min_rate, class->max_rate,
                     class->burst, class->priority, ovs_strerror(error));
    }
    return error;
}

/* Parses Netlink attributes in 'options' for HTB parameters and stores a
 * description of them into 'details'.  The description complies with the
 * specification given in the vswitch database documentation for linux-htb
 * queue details. */
static int
htb_parse_tca_options__(struct nlattr *nl_options, struct htb_class *class)
{
    static const struct nl_policy tca_htb_policy[] = {
        [TCA_HTB_PARMS] = { .type = NL_A_UNSPEC, .optional = false,
                            .min_len = sizeof(struct tc_htb_opt) },
    };

    struct nlattr *attrs[ARRAY_SIZE(tca_htb_policy)];
    const struct tc_htb_opt *htb;

    if (!nl_parse_nested(nl_options, tca_htb_policy,
                         attrs, ARRAY_SIZE(tca_htb_policy))) {
        VLOG_WARN_RL(&rl, "failed to parse HTB class options");
        return EPROTO;
    }

    htb = nl_attr_get(attrs[TCA_HTB_PARMS]);
    class->min_rate = htb->rate.rate;
    class->max_rate = htb->ceil.rate;
    class->burst = tc_ticks_to_bytes(htb->rate.rate, htb->buffer);
    class->priority = htb->prio;
    return 0;
}

static int
htb_parse_tcmsg__(struct ofpbuf *tcmsg, unsigned int *queue_id,
                  struct htb_class *options,
                  struct netdev_queue_stats *stats)
{
    struct nlattr *nl_options;
    unsigned int handle;
    int error;

    error = tc_parse_class(tcmsg, &handle, &nl_options, stats);
    if (!error && queue_id) {
        unsigned int major = tc_get_major(handle);
        unsigned int minor = tc_get_minor(handle);
        if (major == 1 && minor > 0 && minor <= HTB_N_QUEUES) {
            *queue_id = minor - 1;
        } else {
            error = EPROTO;
        }
    }
    if (!error && options) {
        error = htb_parse_tca_options__(nl_options, options);
    }
    return error;
}

static void
htb_parse_qdisc_details__(struct netdev *netdev_,
                          const struct smap *details, struct htb_class *hc)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    const char *max_rate_s;

    max_rate_s = smap_get(details, "max-rate");
    hc->max_rate = max_rate_s ? strtoull(max_rate_s, NULL, 10) / 8 : 0;
    if (!hc->max_rate) {
        enum netdev_features current;

        netdev_linux_read_features(netdev);
        current = !netdev->get_features_error ? netdev->current : 0;
        hc->max_rate = netdev_features_to_bps(current, 100 * 1000 * 1000) / 8;
    }
    hc->min_rate = hc->max_rate;
    hc->burst = 0;
    hc->priority = 0;
}

static int
htb_parse_class_details__(struct netdev *netdev,
                          const struct smap *details, struct htb_class *hc)
{
    const struct htb *htb = htb_get__(netdev);
    const char *min_rate_s = smap_get(details, "min-rate");
    const char *max_rate_s = smap_get(details, "max-rate");
    const char *burst_s = smap_get(details, "burst");
    const char *priority_s = smap_get(details, "priority");
    int mtu, error;

    error = netdev_linux_get_mtu__(netdev_linux_cast(netdev), &mtu);
    if (error) {
        VLOG_WARN_RL(&rl, "cannot parse HTB class on device %s that lacks MTU",
                     netdev_get_name(netdev));
        return error;
    }

    /* HTB requires at least an mtu sized min-rate to send any traffic even
     * on uncongested links. */
    hc->min_rate = min_rate_s ? strtoull(min_rate_s, NULL, 10) / 8 : 0;
    hc->min_rate = MAX(hc->min_rate, mtu);
    hc->min_rate = MIN(hc->min_rate, htb->max_rate);

    /* max-rate */
    hc->max_rate = (max_rate_s
                    ? strtoull(max_rate_s, NULL, 10) / 8
                    : htb->max_rate);
    hc->max_rate = MAX(hc->max_rate, hc->min_rate);
    hc->max_rate = MIN(hc->max_rate, htb->max_rate);

    /* burst
     *
     * According to hints in the documentation that I've read, it is important
     * that 'burst' be at least as big as the largest frame that might be
     * transmitted.  Also, making 'burst' a bit bigger than necessary is OK,
     * but having it a bit too small is a problem.  Since netdev_get_mtu()
     * doesn't include the Ethernet header, we need to add at least 14 (18?) to
     * the MTU.  We actually add 64, instead of 14, as a guard against
     * additional headers get tacked on somewhere that we're not aware of. */
    hc->burst = burst_s ? strtoull(burst_s, NULL, 10) / 8 : 0;
    hc->burst = MAX(hc->burst, mtu + 64);

    /* priority */
    hc->priority = priority_s ? strtoul(priority_s, NULL, 10) : 0;

    return 0;
}

static int
htb_query_class__(const struct netdev *netdev, unsigned int handle,
                  unsigned int parent, struct htb_class *options,
                  struct netdev_queue_stats *stats)
{
    struct ofpbuf *reply;
    int error;

    error = tc_query_class(netdev, handle, parent, &reply);
    if (!error) {
        error = htb_parse_tcmsg__(reply, NULL, options, stats);
        ofpbuf_delete(reply);
    }
    return error;
}

static int
htb_tc_install(struct netdev *netdev, const struct smap *details)
{
    int error;

    error = htb_setup_qdisc__(netdev);
    if (!error) {
        struct htb_class hc;

        htb_parse_qdisc_details__(netdev, details, &hc);
        error = htb_setup_class__(netdev, tc_make_handle(1, 0xfffe),
                                  tc_make_handle(1, 0), &hc);
        if (!error) {
            htb_install__(netdev, hc.max_rate);
        }
    }
    return error;
}

static struct htb_class *
htb_class_cast__(const struct tc_queue *queue)
{
    return CONTAINER_OF(queue, struct htb_class, tc_queue);
}

static void
htb_update_queue__(struct netdev *netdev, unsigned int queue_id,
                   const struct htb_class *hc)
{
    struct htb *htb = htb_get__(netdev);
    size_t hash = hash_int(queue_id, 0);
    struct tc_queue *queue;
    struct htb_class *hcp;

    queue = tc_find_queue__(netdev, queue_id, hash);
    if (queue) {
        hcp = htb_class_cast__(queue);
    } else {
        hcp = xmalloc(sizeof *hcp);
        queue = &hcp->tc_queue;
        queue->queue_id = queue_id;
        queue->created = time_msec();
        hmap_insert(&htb->tc.queues, &queue->hmap_node, hash);
    }

    hcp->min_rate = hc->min_rate;
    hcp->max_rate = hc->max_rate;
    hcp->burst = hc->burst;
    hcp->priority = hc->priority;
}

static int
htb_tc_load(struct netdev *netdev, struct ofpbuf *nlmsg OVS_UNUSED)
{
    struct ofpbuf msg;
    struct nl_dump dump;
    struct htb_class hc;

    /* Get qdisc options. */
    hc.max_rate = 0;
    htb_query_class__(netdev, tc_make_handle(1, 0xfffe), 0, &hc, NULL);
    htb_install__(netdev, hc.max_rate);

    /* Get queues. */
    if (!start_queue_dump(netdev, &dump)) {
        return ENODEV;
    }
    while (nl_dump_next(&dump, &msg)) {
        unsigned int queue_id;

        if (!htb_parse_tcmsg__(&msg, &queue_id, &hc, NULL)) {
            htb_update_queue__(netdev, queue_id, &hc);
        }
    }
    nl_dump_done(&dump);

    return 0;
}

static void
htb_tc_destroy(struct tc *tc)
{
    struct htb *htb = CONTAINER_OF(tc, struct htb, tc);
    struct htb_class *hc, *next;

    HMAP_FOR_EACH_SAFE (hc, next, tc_queue.hmap_node, &htb->tc.queues) {
        hmap_remove(&htb->tc.queues, &hc->tc_queue.hmap_node);
        free(hc);
    }
    tc_destroy(tc);
    free(htb);
}

static int
htb_qdisc_get(const struct netdev *netdev, struct smap *details)
{
    const struct htb *htb = htb_get__(netdev);
    smap_add_format(details, "max-rate", "%llu", 8ULL * htb->max_rate);
    return 0;
}

static int
htb_qdisc_set(struct netdev *netdev, const struct smap *details)
{
    struct htb_class hc;
    int error;

    htb_parse_qdisc_details__(netdev, details, &hc);
    error = htb_setup_class__(netdev, tc_make_handle(1, 0xfffe),
                              tc_make_handle(1, 0), &hc);
    if (!error) {
        htb_get__(netdev)->max_rate = hc.max_rate;
    }
    return error;
}

static int
htb_class_get(const struct netdev *netdev OVS_UNUSED,
              const struct tc_queue *queue, struct smap *details)
{
    const struct htb_class *hc = htb_class_cast__(queue);

    smap_add_format(details, "min-rate", "%llu", 8ULL * hc->min_rate);
    if (hc->min_rate != hc->max_rate) {
        smap_add_format(details, "max-rate", "%llu", 8ULL * hc->max_rate);
    }
    smap_add_format(details, "burst", "%llu", 8ULL * hc->burst);
    if (hc->priority) {
        smap_add_format(details, "priority", "%u", hc->priority);
    }
    return 0;
}

static int
htb_class_set(struct netdev *netdev, unsigned int queue_id,
              const struct smap *details)
{
    struct htb_class hc;
    int error;

    error = htb_parse_class_details__(netdev, details, &hc);
    if (error) {
        return error;
    }

    error = htb_setup_class__(netdev, tc_make_handle(1, queue_id + 1),
                              tc_make_handle(1, 0xfffe), &hc);
    if (error) {
        return error;
    }

    htb_update_queue__(netdev, queue_id, &hc);
    return 0;
}

static int
htb_class_delete(struct netdev *netdev, struct tc_queue *queue)
{
    struct htb_class *hc = htb_class_cast__(queue);
    struct htb *htb = htb_get__(netdev);
    int error;

    error = tc_delete_class(netdev, tc_make_handle(1, queue->queue_id + 1));
    if (!error) {
        hmap_remove(&htb->tc.queues, &hc->tc_queue.hmap_node);
        free(hc);
    }
    return error;
}

static int
htb_class_get_stats(const struct netdev *netdev, const struct tc_queue *queue,
                    struct netdev_queue_stats *stats)
{
    return htb_query_class__(netdev, tc_make_handle(1, queue->queue_id + 1),
                             tc_make_handle(1, 0xfffe), NULL, stats);
}

static int
htb_class_dump_stats(const struct netdev *netdev OVS_UNUSED,
                     const struct ofpbuf *nlmsg,
                     netdev_dump_queue_stats_cb *cb, void *aux)
{
    struct netdev_queue_stats stats;
    unsigned int handle, major, minor;
    int error;

    error = tc_parse_class(nlmsg, &handle, NULL, &stats);
    if (error) {
        return error;
    }

    major = tc_get_major(handle);
    minor = tc_get_minor(handle);
    if (major == 1 && minor > 0 && minor <= HTB_N_QUEUES) {
        (*cb)(minor - 1, &stats, aux);
    }
    return 0;
}

static const struct tc_ops tc_ops_htb = {
    "htb",                      /* linux_name */
    "linux-htb",                /* ovs_name */
    HTB_N_QUEUES,               /* n_queues */
    htb_tc_install,
    htb_tc_load,
    htb_tc_destroy,
    htb_qdisc_get,
    htb_qdisc_set,
    htb_class_get,
    htb_class_set,
    htb_class_delete,
    htb_class_get_stats,
    htb_class_dump_stats
};

/* "linux-hfsc" traffic control class. */

#define HFSC_N_QUEUES 0xf000

struct hfsc {
    struct tc tc;
    uint32_t max_rate;
};

struct hfsc_class {
    struct tc_queue tc_queue;
    uint32_t min_rate;
    uint32_t max_rate;
};

static struct hfsc *
hfsc_get__(const struct netdev *netdev_)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    return CONTAINER_OF(netdev->tc, struct hfsc, tc);
}

static struct hfsc_class *
hfsc_class_cast__(const struct tc_queue *queue)
{
    return CONTAINER_OF(queue, struct hfsc_class, tc_queue);
}

static void
hfsc_install__(struct netdev *netdev_, uint32_t max_rate)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    struct hfsc *hfsc;

    hfsc = xmalloc(sizeof *hfsc);
    tc_init(&hfsc->tc, &tc_ops_hfsc);
    hfsc->max_rate = max_rate;
    netdev->tc = &hfsc->tc;
}

static void
hfsc_update_queue__(struct netdev *netdev, unsigned int queue_id,
                    const struct hfsc_class *hc)
{
    size_t hash;
    struct hfsc *hfsc;
    struct hfsc_class *hcp;
    struct tc_queue *queue;

    hfsc = hfsc_get__(netdev);
    hash = hash_int(queue_id, 0);

    queue = tc_find_queue__(netdev, queue_id, hash);
    if (queue) {
        hcp = hfsc_class_cast__(queue);
    } else {
        hcp             = xmalloc(sizeof *hcp);
        queue           = &hcp->tc_queue;
        queue->queue_id = queue_id;
        queue->created  = time_msec();
        hmap_insert(&hfsc->tc.queues, &queue->hmap_node, hash);
    }

    hcp->min_rate = hc->min_rate;
    hcp->max_rate = hc->max_rate;
}

static int
hfsc_parse_tca_options__(struct nlattr *nl_options, struct hfsc_class *class)
{
    const struct tc_service_curve *rsc, *fsc, *usc;
    static const struct nl_policy tca_hfsc_policy[] = {
        [TCA_HFSC_RSC] = {
            .type      = NL_A_UNSPEC,
            .optional  = false,
            .min_len   = sizeof(struct tc_service_curve),
        },
        [TCA_HFSC_FSC] = {
            .type      = NL_A_UNSPEC,
            .optional  = false,
            .min_len   = sizeof(struct tc_service_curve),
        },
        [TCA_HFSC_USC] = {
            .type      = NL_A_UNSPEC,
            .optional  = false,
            .min_len   = sizeof(struct tc_service_curve),
        },
    };
    struct nlattr *attrs[ARRAY_SIZE(tca_hfsc_policy)];

    if (!nl_parse_nested(nl_options, tca_hfsc_policy,
                         attrs, ARRAY_SIZE(tca_hfsc_policy))) {
        VLOG_WARN_RL(&rl, "failed to parse HFSC class options");
        return EPROTO;
    }

    rsc = nl_attr_get(attrs[TCA_HFSC_RSC]);
    fsc = nl_attr_get(attrs[TCA_HFSC_FSC]);
    usc = nl_attr_get(attrs[TCA_HFSC_USC]);

    if (rsc->m1 != 0 || rsc->d != 0 ||
        fsc->m1 != 0 || fsc->d != 0 ||
        usc->m1 != 0 || usc->d != 0) {
        VLOG_WARN_RL(&rl, "failed to parse HFSC class options. "
                     "Non-linear service curves are not supported.");
        return EPROTO;
    }

    if (rsc->m2 != fsc->m2) {
        VLOG_WARN_RL(&rl, "failed to parse HFSC class options. "
                     "Real-time service curves are not supported ");
        return EPROTO;
    }

    if (rsc->m2 > usc->m2) {
        VLOG_WARN_RL(&rl, "failed to parse HFSC class options. "
                     "Min-rate service curve is greater than "
                     "the max-rate service curve.");
        return EPROTO;
    }

    class->min_rate = fsc->m2;
    class->max_rate = usc->m2;
    return 0;
}

static int
hfsc_parse_tcmsg__(struct ofpbuf *tcmsg, unsigned int *queue_id,
                   struct hfsc_class *options,
                   struct netdev_queue_stats *stats)
{
    int error;
    unsigned int handle;
    struct nlattr *nl_options;

    error = tc_parse_class(tcmsg, &handle, &nl_options, stats);
    if (error) {
        return error;
    }

    if (queue_id) {
        unsigned int major, minor;

        major = tc_get_major(handle);
        minor = tc_get_minor(handle);
        if (major == 1 && minor > 0 && minor <= HFSC_N_QUEUES) {
            *queue_id = minor - 1;
        } else {
            return EPROTO;
        }
    }

    if (options) {
        error = hfsc_parse_tca_options__(nl_options, options);
    }

    return error;
}

static int
hfsc_query_class__(const struct netdev *netdev, unsigned int handle,
                   unsigned int parent, struct hfsc_class *options,
                   struct netdev_queue_stats *stats)
{
    int error;
    struct ofpbuf *reply;

    error = tc_query_class(netdev, handle, parent, &reply);
    if (error) {
        return error;
    }

    error = hfsc_parse_tcmsg__(reply, NULL, options, stats);
    ofpbuf_delete(reply);
    return error;
}

static void
hfsc_parse_qdisc_details__(struct netdev *netdev_, const struct smap *details,
                           struct hfsc_class *class)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    uint32_t max_rate;
    const char *max_rate_s;

    max_rate_s = smap_get(details, "max-rate");
    max_rate   = max_rate_s ? strtoull(max_rate_s, NULL, 10) / 8 : 0;

    if (!max_rate) {
        enum netdev_features current;

        netdev_linux_read_features(netdev);
        current = !netdev->get_features_error ? netdev->current : 0;
        max_rate = netdev_features_to_bps(current, 100 * 1000 * 1000) / 8;
    }

    class->min_rate = max_rate;
    class->max_rate = max_rate;
}

static int
hfsc_parse_class_details__(struct netdev *netdev,
                           const struct smap *details,
                           struct hfsc_class * class)
{
    const struct hfsc *hfsc;
    uint32_t min_rate, max_rate;
    const char *min_rate_s, *max_rate_s;

    hfsc       = hfsc_get__(netdev);
    min_rate_s = smap_get(details, "min-rate");
    max_rate_s = smap_get(details, "max-rate");

    min_rate = min_rate_s ? strtoull(min_rate_s, NULL, 10) / 8 : 0;
    min_rate = MAX(min_rate, 1);
    min_rate = MIN(min_rate, hfsc->max_rate);

    max_rate = (max_rate_s
                ? strtoull(max_rate_s, NULL, 10) / 8
                : hfsc->max_rate);
    max_rate = MAX(max_rate, min_rate);
    max_rate = MIN(max_rate, hfsc->max_rate);

    class->min_rate = min_rate;
    class->max_rate = max_rate;

    return 0;
}

/* Create an HFSC qdisc.
 *
 * Equivalent to "tc qdisc add dev <dev> root handle 1: hfsc default 1". */
static int
hfsc_setup_qdisc__(struct netdev * netdev)
{
    struct tcmsg *tcmsg;
    struct ofpbuf request;
    struct tc_hfsc_qopt opt;

    tc_del_qdisc(netdev);

    tcmsg = tc_make_request(netdev, RTM_NEWQDISC,
                            NLM_F_EXCL | NLM_F_CREATE, &request);

    if (!tcmsg) {
        return ENODEV;
    }

    tcmsg->tcm_handle = tc_make_handle(1, 0);
    tcmsg->tcm_parent = TC_H_ROOT;

    memset(&opt, 0, sizeof opt);
    opt.defcls = 1;

    nl_msg_put_string(&request, TCA_KIND, "hfsc");
    nl_msg_put_unspec(&request, TCA_OPTIONS, &opt, sizeof opt);

    return tc_transact(&request, NULL);
}

/* Create an HFSC class.
 *
 * Equivalent to "tc class add <dev> parent <parent> classid <handle> hfsc
 * sc rate <min_rate> ul rate <max_rate>" */
static int
hfsc_setup_class__(struct netdev *netdev, unsigned int handle,
                   unsigned int parent, struct hfsc_class *class)
{
    int error;
    size_t opt_offset;
    struct tcmsg *tcmsg;
    struct ofpbuf request;
    struct tc_service_curve min, max;

    tcmsg = tc_make_request(netdev, RTM_NEWTCLASS, NLM_F_CREATE, &request);

    if (!tcmsg) {
        return ENODEV;
    }

    tcmsg->tcm_handle = handle;
    tcmsg->tcm_parent = parent;

    min.m1 = 0;
    min.d  = 0;
    min.m2 = class->min_rate;

    max.m1 = 0;
    max.d  = 0;
    max.m2 = class->max_rate;

    nl_msg_put_string(&request, TCA_KIND, "hfsc");
    opt_offset = nl_msg_start_nested(&request, TCA_OPTIONS);
    nl_msg_put_unspec(&request, TCA_HFSC_RSC, &min, sizeof min);
    nl_msg_put_unspec(&request, TCA_HFSC_FSC, &min, sizeof min);
    nl_msg_put_unspec(&request, TCA_HFSC_USC, &max, sizeof max);
    nl_msg_end_nested(&request, opt_offset);

    error = tc_transact(&request, NULL);
    if (error) {
        VLOG_WARN_RL(&rl, "failed to replace %s class %u:%u, parent %u:%u, "
                     "min-rate %ubps, max-rate %ubps (%s)",
                     netdev_get_name(netdev),
                     tc_get_major(handle), tc_get_minor(handle),
                     tc_get_major(parent), tc_get_minor(parent),
                     class->min_rate, class->max_rate, ovs_strerror(error));
    }

    return error;
}

static int
hfsc_tc_install(struct netdev *netdev, const struct smap *details)
{
    int error;
    struct hfsc_class class;

    error = hfsc_setup_qdisc__(netdev);

    if (error) {
        return error;
    }

    hfsc_parse_qdisc_details__(netdev, details, &class);
    error = hfsc_setup_class__(netdev, tc_make_handle(1, 0xfffe),
                               tc_make_handle(1, 0), &class);

    if (error) {
        return error;
    }

    hfsc_install__(netdev, class.max_rate);
    return 0;
}

static int
hfsc_tc_load(struct netdev *netdev, struct ofpbuf *nlmsg OVS_UNUSED)
{
    struct ofpbuf msg;
    struct nl_dump dump;
    struct hfsc_class hc;

    hc.max_rate = 0;
    hfsc_query_class__(netdev, tc_make_handle(1, 0xfffe), 0, &hc, NULL);
    hfsc_install__(netdev, hc.max_rate);

    if (!start_queue_dump(netdev, &dump)) {
        return ENODEV;
    }

    while (nl_dump_next(&dump, &msg)) {
        unsigned int queue_id;

        if (!hfsc_parse_tcmsg__(&msg, &queue_id, &hc, NULL)) {
            hfsc_update_queue__(netdev, queue_id, &hc);
        }
    }

    nl_dump_done(&dump);
    return 0;
}

static void
hfsc_tc_destroy(struct tc *tc)
{
    struct hfsc *hfsc;
    struct hfsc_class *hc, *next;

    hfsc = CONTAINER_OF(tc, struct hfsc, tc);

    HMAP_FOR_EACH_SAFE (hc, next, tc_queue.hmap_node, &hfsc->tc.queues) {
        hmap_remove(&hfsc->tc.queues, &hc->tc_queue.hmap_node);
        free(hc);
    }

    tc_destroy(tc);
    free(hfsc);
}

static int
hfsc_qdisc_get(const struct netdev *netdev, struct smap *details)
{
    const struct hfsc *hfsc;
    hfsc = hfsc_get__(netdev);
    smap_add_format(details, "max-rate", "%llu", 8ULL * hfsc->max_rate);
    return 0;
}

static int
hfsc_qdisc_set(struct netdev *netdev, const struct smap *details)
{
    int error;
    struct hfsc_class class;

    hfsc_parse_qdisc_details__(netdev, details, &class);
    error = hfsc_setup_class__(netdev, tc_make_handle(1, 0xfffe),
                               tc_make_handle(1, 0), &class);

    if (!error) {
        hfsc_get__(netdev)->max_rate = class.max_rate;
    }

    return error;
}

static int
hfsc_class_get(const struct netdev *netdev OVS_UNUSED,
              const struct tc_queue *queue, struct smap *details)
{
    const struct hfsc_class *hc;

    hc = hfsc_class_cast__(queue);
    smap_add_format(details, "min-rate", "%llu", 8ULL * hc->min_rate);
    if (hc->min_rate != hc->max_rate) {
        smap_add_format(details, "max-rate", "%llu", 8ULL * hc->max_rate);
    }
    return 0;
}

static int
hfsc_class_set(struct netdev *netdev, unsigned int queue_id,
               const struct smap *details)
{
    int error;
    struct hfsc_class class;

    error = hfsc_parse_class_details__(netdev, details, &class);
    if (error) {
        return error;
    }

    error = hfsc_setup_class__(netdev, tc_make_handle(1, queue_id + 1),
                               tc_make_handle(1, 0xfffe), &class);
    if (error) {
        return error;
    }

    hfsc_update_queue__(netdev, queue_id, &class);
    return 0;
}

static int
hfsc_class_delete(struct netdev *netdev, struct tc_queue *queue)
{
    int error;
    struct hfsc *hfsc;
    struct hfsc_class *hc;

    hc   = hfsc_class_cast__(queue);
    hfsc = hfsc_get__(netdev);

    error = tc_delete_class(netdev, tc_make_handle(1, queue->queue_id + 1));
    if (!error) {
        hmap_remove(&hfsc->tc.queues, &hc->tc_queue.hmap_node);
        free(hc);
    }
    return error;
}

static int
hfsc_class_get_stats(const struct netdev *netdev, const struct tc_queue *queue,
                     struct netdev_queue_stats *stats)
{
    return hfsc_query_class__(netdev, tc_make_handle(1, queue->queue_id + 1),
                             tc_make_handle(1, 0xfffe), NULL, stats);
}

static int
hfsc_class_dump_stats(const struct netdev *netdev OVS_UNUSED,
                      const struct ofpbuf *nlmsg,
                      netdev_dump_queue_stats_cb *cb, void *aux)
{
    struct netdev_queue_stats stats;
    unsigned int handle, major, minor;
    int error;

    error = tc_parse_class(nlmsg, &handle, NULL, &stats);
    if (error) {
        return error;
    }

    major = tc_get_major(handle);
    minor = tc_get_minor(handle);
    if (major == 1 && minor > 0 && minor <= HFSC_N_QUEUES) {
        (*cb)(minor - 1, &stats, aux);
    }
    return 0;
}

static const struct tc_ops tc_ops_hfsc = {
    "hfsc",                     /* linux_name */
    "linux-hfsc",               /* ovs_name */
    HFSC_N_QUEUES,              /* n_queues */
    hfsc_tc_install,            /* tc_install */
    hfsc_tc_load,               /* tc_load */
    hfsc_tc_destroy,            /* tc_destroy */
    hfsc_qdisc_get,             /* qdisc_get */
    hfsc_qdisc_set,             /* qdisc_set */
    hfsc_class_get,             /* class_get */
    hfsc_class_set,             /* class_set */
    hfsc_class_delete,          /* class_delete */
    hfsc_class_get_stats,       /* class_get_stats */
    hfsc_class_dump_stats       /* class_dump_stats */
};

/* "linux-default" traffic control class.
 *
 * This class represents the default, unnamed Linux qdisc.  It corresponds to
 * the "" (empty string) QoS type in the OVS database. */

static void
default_install__(struct netdev *netdev_)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    static const struct tc tc = TC_INITIALIZER(&tc, &tc_ops_default);

    /* Nothing but a tc class implementation is allowed to write to a tc.  This
     * class never does that, so we can legitimately use a const tc object. */
    netdev->tc = CONST_CAST(struct tc *, &tc);
}

static int
default_tc_install(struct netdev *netdev,
                   const struct smap *details OVS_UNUSED)
{
    default_install__(netdev);
    return 0;
}

static int
default_tc_load(struct netdev *netdev, struct ofpbuf *nlmsg OVS_UNUSED)
{
    default_install__(netdev);
    return 0;
}

static const struct tc_ops tc_ops_default = {
    NULL,                       /* linux_name */
    "",                         /* ovs_name */
    0,                          /* n_queues */
    default_tc_install,
    default_tc_load,
    NULL,                       /* tc_destroy */
    NULL,                       /* qdisc_get */
    NULL,                       /* qdisc_set */
    NULL,                       /* class_get */
    NULL,                       /* class_set */
    NULL,                       /* class_delete */
    NULL,                       /* class_get_stats */
    NULL                        /* class_dump_stats */
};

/* "linux-other" traffic control class.
 *
 * */

static int
other_tc_load(struct netdev *netdev_, struct ofpbuf *nlmsg OVS_UNUSED)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    static const struct tc tc = TC_INITIALIZER(&tc, &tc_ops_other);

    /* Nothing but a tc class implementation is allowed to write to a tc.  This
     * class never does that, so we can legitimately use a const tc object. */
    netdev->tc = CONST_CAST(struct tc *, &tc);
    return 0;
}

static const struct tc_ops tc_ops_other = {
    NULL,                       /* linux_name */
    "linux-other",              /* ovs_name */
    0,                          /* n_queues */
    NULL,                       /* tc_install */
    other_tc_load,
    NULL,                       /* tc_destroy */
    NULL,                       /* qdisc_get */
    NULL,                       /* qdisc_set */
    NULL,                       /* class_get */
    NULL,                       /* class_set */
    NULL,                       /* class_delete */
    NULL,                       /* class_get_stats */
    NULL                        /* class_dump_stats */
};

/* Traffic control. */

/* Number of kernel "tc" ticks per second. */
static double ticks_per_s;

/* Number of kernel "jiffies" per second.  This is used for the purpose of
 * computing buffer sizes.  Generally kernel qdiscs need to be able to buffer
 * one jiffy's worth of data.
 *
 * There are two possibilities here:
 *
 *    - 'buffer_hz' is the kernel's real timer tick rate, a small number in the
 *      approximate range of 100 to 1024.  That means that we really need to
 *      make sure that the qdisc can buffer that much data.
 *
 *    - 'buffer_hz' is an absurdly large number.  That means that the kernel
 *      has finely granular timers and there's no need to fudge additional room
 *      for buffers.  (There's no extra effort needed to implement that: the
 *      large 'buffer_hz' is used as a divisor, so practically any number will
 *      come out as 0 in the division.  Small integer results in the case of
 *      really high dividends won't have any real effect anyhow.)
 */
static unsigned int buffer_hz;

/* Returns tc handle 'major':'minor'. */
static unsigned int
tc_make_handle(unsigned int major, unsigned int minor)
{
    return TC_H_MAKE(major << 16, minor);
}

/* Returns the major number from 'handle'. */
static unsigned int
tc_get_major(unsigned int handle)
{
    return TC_H_MAJ(handle) >> 16;
}

/* Returns the minor number from 'handle'. */
static unsigned int
tc_get_minor(unsigned int handle)
{
    return TC_H_MIN(handle);
}

static struct tcmsg *
tc_make_request(const struct netdev *netdev, int type, unsigned int flags,
                struct ofpbuf *request)
{
    struct tcmsg *tcmsg;
    int ifindex;
    int error;

    error = get_ifindex(netdev, &ifindex);
    if (error) {
        return NULL;
    }

    ofpbuf_init(request, 512);
    nl_msg_put_nlmsghdr(request, sizeof *tcmsg, type, NLM_F_REQUEST | flags);
    tcmsg = ofpbuf_put_zeros(request, sizeof *tcmsg);
    tcmsg->tcm_family = AF_UNSPEC;
    tcmsg->tcm_ifindex = ifindex;
    /* Caller should fill in tcmsg->tcm_handle. */
    /* Caller should fill in tcmsg->tcm_parent. */

    return tcmsg;
}

static int
tc_transact(struct ofpbuf *request, struct ofpbuf **replyp)
{
    int error = nl_transact(NETLINK_ROUTE, request, replyp);
    ofpbuf_uninit(request);
    return error;
}

/* Adds or deletes a root ingress qdisc on 'netdev'.  We use this for
 * policing configuration.
 *
 * This function is equivalent to running the following when 'add' is true:
 *     /sbin/tc qdisc add dev <devname> handle ffff: ingress
 *
 * This function is equivalent to running the following when 'add' is false:
 *     /sbin/tc qdisc del dev <devname> handle ffff: ingress
 *
 * The configuration and stats may be seen with the following command:
 *     /sbin/tc -s qdisc show dev <devname>
 *
 * Returns 0 if successful, otherwise a positive errno value.
 */
static int
tc_add_del_ingress_qdisc(struct netdev *netdev, bool add)
{
    struct ofpbuf request;
    struct tcmsg *tcmsg;
    int error;
    int type = add ? RTM_NEWQDISC : RTM_DELQDISC;
    int flags = add ? NLM_F_EXCL | NLM_F_CREATE : 0;

    tcmsg = tc_make_request(netdev, type, flags, &request);
    if (!tcmsg) {
        return ENODEV;
    }
    tcmsg->tcm_handle = tc_make_handle(0xffff, 0);
    tcmsg->tcm_parent = TC_H_INGRESS;
    nl_msg_put_string(&request, TCA_KIND, "ingress");
    nl_msg_put_unspec(&request, TCA_OPTIONS, NULL, 0);

    error = tc_transact(&request, NULL);
    if (error) {
        /* If we're deleting the qdisc, don't worry about some of the
         * error conditions. */
        if (!add && (error == ENOENT || error == EINVAL)) {
            return 0;
        }
        return error;
    }

    return 0;
}

/* Adds a policer to 'netdev' with a rate of 'kbits_rate' and a burst size
 * of 'kbits_burst'.
 *
 * This function is equivalent to running:
 *     /sbin/tc filter add dev <devname> parent ffff: protocol all prio 49
 *              basic police rate <kbits_rate>kbit burst <kbits_burst>k
 *              mtu 65535 drop
 *
 * The configuration and stats may be seen with the following command:
 *     /sbin/tc -s filter show <devname> eth0 parent ffff:
 *
 * Returns 0 if successful, otherwise a positive errno value.
 */
static int
tc_add_policer(struct netdev *netdev, int kbits_rate, int kbits_burst)
{
    struct tc_police tc_police;
    struct ofpbuf request;
    struct tcmsg *tcmsg;
    size_t basic_offset;
    size_t police_offset;
    int error;
    int mtu = 65535;

    memset(&tc_police, 0, sizeof tc_police);
    tc_police.action = TC_POLICE_SHOT;
    tc_police.mtu = mtu;
    tc_fill_rate(&tc_police.rate, (kbits_rate * 1000)/8, mtu);
    tc_police.burst = tc_bytes_to_ticks(tc_police.rate.rate,
                                        kbits_burst * 1024);

    tcmsg = tc_make_request(netdev, RTM_NEWTFILTER,
                            NLM_F_EXCL | NLM_F_CREATE, &request);
    if (!tcmsg) {
        return ENODEV;
    }
    tcmsg->tcm_parent = tc_make_handle(0xffff, 0);
    tcmsg->tcm_info = tc_make_handle(49,
                                     (OVS_FORCE uint16_t) htons(ETH_P_ALL));

    nl_msg_put_string(&request, TCA_KIND, "basic");
    basic_offset = nl_msg_start_nested(&request, TCA_OPTIONS);
    police_offset = nl_msg_start_nested(&request, TCA_BASIC_POLICE);
    nl_msg_put_unspec(&request, TCA_POLICE_TBF, &tc_police, sizeof tc_police);
    tc_put_rtab(&request, TCA_POLICE_RATE, &tc_police.rate);
    nl_msg_end_nested(&request, police_offset);
    nl_msg_end_nested(&request, basic_offset);

    error = tc_transact(&request, NULL);
    if (error) {
        return error;
    }

    return 0;
}

static void
read_psched(void)
{
    /* The values in psched are not individually very meaningful, but they are
     * important.  The tables below show some values seen in the wild.
     *
     * Some notes:
     *
     *   - "c" has always been a constant 1000000 since at least Linux 2.4.14.
     *     (Before that, there are hints that it was 1000000000.)
     *
     *   - "d" can be unrealistically large, see the comment on 'buffer_hz'
     *     above.
     *
     *                        /proc/net/psched
     *     -----------------------------------
     * [1] 000c8000 000f4240 000f4240 00000064
     * [2] 000003e8 00000400 000f4240 3b9aca00
     * [3] 000003e8 00000400 000f4240 3b9aca00
     * [4] 000003e8 00000400 000f4240 00000064
     * [5] 000003e8 00000040 000f4240 3b9aca00
     * [6] 000003e8 00000040 000f4240 000000f9
     *
     *           a         b          c             d ticks_per_s     buffer_hz
     *     ------- --------- ---------- ------------- ----------- -------------
     * [1] 819,200 1,000,000  1,000,000           100     819,200           100
     * [2]   1,000     1,024  1,000,000 1,000,000,000     976,562 1,000,000,000
     * [3]   1,000     1,024  1,000,000 1,000,000,000     976,562 1,000,000,000
     * [4]   1,000     1,024  1,000,000           100     976,562           100
     * [5]   1,000        64  1,000,000 1,000,000,000  15,625,000 1,000,000,000
     * [6]   1,000        64  1,000,000           249  15,625,000           249
     *
     * [1] 2.6.18-128.1.6.el5.xs5.5.0.505.1024xen from XenServer 5.5.0-24648p
     * [2] 2.6.26-1-686-bigmem from Debian lenny
     * [3] 2.6.26-2-sparc64 from Debian lenny
     * [4] 2.6.27.42-0.1.1.xs5.6.810.44.111163xen from XenServer 5.6.810-31078p
     * [5] 2.6.32.21.22 (approx.) from Ubuntu 10.04 on VMware Fusion
     * [6] 2.6.34 from kernel.org on KVM
     */
    static struct ovsthread_once once = OVSTHREAD_ONCE_INITIALIZER;
    static const char fn[] = "/proc/net/psched";
    unsigned int a, b, c, d;
    FILE *stream;

    if (!ovsthread_once_start(&once)) {
        return;
    }

    ticks_per_s = 1.0;
    buffer_hz = 100;

    stream = fopen(fn, "r");
    if (!stream) {
        VLOG_WARN("%s: open failed: %s", fn, ovs_strerror(errno));
        goto exit;
    }

    if (fscanf(stream, "%x %x %x %x", &a, &b, &c, &d) != 4) {
        VLOG_WARN("%s: read failed", fn);
        fclose(stream);
        goto exit;
    }
    VLOG_DBG("%s: psched parameters are: %u %u %u %u", fn, a, b, c, d);
    fclose(stream);

    if (!a || !c) {
        VLOG_WARN("%s: invalid scheduler parameters", fn);
        goto exit;
    }

    ticks_per_s = (double) a * c / b;
    if (c == 1000000) {
        buffer_hz = d;
    } else {
        VLOG_WARN("%s: unexpected psched parameters: %u %u %u %u",
                  fn, a, b, c, d);
    }
    VLOG_DBG("%s: ticks_per_s=%f buffer_hz=%u", fn, ticks_per_s, buffer_hz);

exit:
    ovsthread_once_done(&once);
}

/* Returns the number of bytes that can be transmitted in 'ticks' ticks at a
 * rate of 'rate' bytes per second. */
static unsigned int
tc_ticks_to_bytes(unsigned int rate, unsigned int ticks)
{
    read_psched();
    return (rate * ticks) / ticks_per_s;
}

/* Returns the number of ticks that it would take to transmit 'size' bytes at a
 * rate of 'rate' bytes per second. */
static unsigned int
tc_bytes_to_ticks(unsigned int rate, unsigned int size)
{
    read_psched();
    return rate ? ((unsigned long long int) ticks_per_s * size) / rate : 0;
}

/* Returns the number of bytes that need to be reserved for qdisc buffering at
 * a transmission rate of 'rate' bytes per second. */
static unsigned int
tc_buffer_per_jiffy(unsigned int rate)
{
    read_psched();
    return rate / buffer_hz;
}

/* Given Netlink 'msg' that describes a qdisc, extracts the name of the qdisc,
 * e.g. "htb", into '*kind' (if it is nonnull).  If 'options' is nonnull,
 * extracts 'msg''s TCA_OPTIONS attributes into '*options' if it is present or
 * stores NULL into it if it is absent.
 *
 * '*kind' and '*options' point into 'msg', so they are owned by whoever owns
 * 'msg'.
 *
 * Returns 0 if successful, otherwise a positive errno value. */
static int
tc_parse_qdisc(const struct ofpbuf *msg, const char **kind,
               struct nlattr **options)
{
    static const struct nl_policy tca_policy[] = {
        [TCA_KIND] = { .type = NL_A_STRING, .optional = false },
        [TCA_OPTIONS] = { .type = NL_A_NESTED, .optional = true },
    };
    struct nlattr *ta[ARRAY_SIZE(tca_policy)];

    if (!nl_policy_parse(msg, NLMSG_HDRLEN + sizeof(struct tcmsg),
                         tca_policy, ta, ARRAY_SIZE(ta))) {
        VLOG_WARN_RL(&rl, "failed to parse qdisc message");
        goto error;
    }

    if (kind) {
        *kind = nl_attr_get_string(ta[TCA_KIND]);
    }

    if (options) {
        *options = ta[TCA_OPTIONS];
    }

    return 0;

error:
    if (kind) {
        *kind = NULL;
    }
    if (options) {
        *options = NULL;
    }
    return EPROTO;
}

/* Given Netlink 'msg' that describes a class, extracts the queue ID (e.g. the
 * minor number of its class ID) into '*queue_id', its TCA_OPTIONS attribute
 * into '*options', and its queue statistics into '*stats'.  Any of the output
 * arguments may be null.
 *
 * Returns 0 if successful, otherwise a positive errno value. */
static int
tc_parse_class(const struct ofpbuf *msg, unsigned int *handlep,
               struct nlattr **options, struct netdev_queue_stats *stats)
{
    static const struct nl_policy tca_policy[] = {
        [TCA_OPTIONS] = { .type = NL_A_NESTED, .optional = false },
        [TCA_STATS2] = { .type = NL_A_NESTED, .optional = false },
    };
    struct nlattr *ta[ARRAY_SIZE(tca_policy)];

    if (!nl_policy_parse(msg, NLMSG_HDRLEN + sizeof(struct tcmsg),
                         tca_policy, ta, ARRAY_SIZE(ta))) {
        VLOG_WARN_RL(&rl, "failed to parse class message");
        goto error;
    }

    if (handlep) {
        struct tcmsg *tc = ofpbuf_at_assert(msg, NLMSG_HDRLEN, sizeof *tc);
        *handlep = tc->tcm_handle;
    }

    if (options) {
        *options = ta[TCA_OPTIONS];
    }

    if (stats) {
        const struct gnet_stats_queue *gsq;
        struct gnet_stats_basic gsb;

        static const struct nl_policy stats_policy[] = {
            [TCA_STATS_BASIC] = { .type = NL_A_UNSPEC, .optional = false,
                                  .min_len = sizeof gsb },
            [TCA_STATS_QUEUE] = { .type = NL_A_UNSPEC, .optional = false,
                                  .min_len = sizeof *gsq },
        };
        struct nlattr *sa[ARRAY_SIZE(stats_policy)];

        if (!nl_parse_nested(ta[TCA_STATS2], stats_policy,
                             sa, ARRAY_SIZE(sa))) {
            VLOG_WARN_RL(&rl, "failed to parse class stats");
            goto error;
        }

        /* Alignment issues screw up the length of struct gnet_stats_basic on
         * some arch/bitsize combinations.  Newer versions of Linux have a
         * struct gnet_stats_basic_packed, but we can't depend on that.  The
         * easiest thing to do is just to make a copy. */
        memset(&gsb, 0, sizeof gsb);
        memcpy(&gsb, nl_attr_get(sa[TCA_STATS_BASIC]),
               MIN(nl_attr_get_size(sa[TCA_STATS_BASIC]), sizeof gsb));
        stats->tx_bytes = gsb.bytes;
        stats->tx_packets = gsb.packets;

        gsq = nl_attr_get(sa[TCA_STATS_QUEUE]);
        stats->tx_errors = gsq->drops;
    }

    return 0;

error:
    if (options) {
        *options = NULL;
    }
    if (stats) {
        memset(stats, 0, sizeof *stats);
    }
    return EPROTO;
}

/* Queries the kernel for class with identifier 'handle' and parent 'parent'
 * on 'netdev'. */
static int
tc_query_class(const struct netdev *netdev,
               unsigned int handle, unsigned int parent,
               struct ofpbuf **replyp)
{
    struct ofpbuf request;
    struct tcmsg *tcmsg;
    int error;

    tcmsg = tc_make_request(netdev, RTM_GETTCLASS, NLM_F_ECHO, &request);
    if (!tcmsg) {
        return ENODEV;
    }
    tcmsg->tcm_handle = handle;
    tcmsg->tcm_parent = parent;

    error = tc_transact(&request, replyp);
    if (error) {
        VLOG_WARN_RL(&rl, "query %s class %u:%u (parent %u:%u) failed (%s)",
                     netdev_get_name(netdev),
                     tc_get_major(handle), tc_get_minor(handle),
                     tc_get_major(parent), tc_get_minor(parent),
                     ovs_strerror(error));
    }
    return error;
}

/* Equivalent to "tc class del dev <name> handle <handle>". */
static int
tc_delete_class(const struct netdev *netdev, unsigned int handle)
{
    struct ofpbuf request;
    struct tcmsg *tcmsg;
    int error;

    tcmsg = tc_make_request(netdev, RTM_DELTCLASS, 0, &request);
    if (!tcmsg) {
        return ENODEV;
    }
    tcmsg->tcm_handle = handle;
    tcmsg->tcm_parent = 0;

    error = tc_transact(&request, NULL);
    if (error) {
        VLOG_WARN_RL(&rl, "delete %s class %u:%u failed (%s)",
                     netdev_get_name(netdev),
                     tc_get_major(handle), tc_get_minor(handle),
                     ovs_strerror(error));
    }
    return error;
}

/* Equivalent to "tc qdisc del dev <name> root". */
static int
tc_del_qdisc(struct netdev *netdev_)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    struct ofpbuf request;
    struct tcmsg *tcmsg;
    int error;

    tcmsg = tc_make_request(netdev_, RTM_DELQDISC, 0, &request);
    if (!tcmsg) {
        return ENODEV;
    }
    tcmsg->tcm_handle = tc_make_handle(1, 0);
    tcmsg->tcm_parent = TC_H_ROOT;

    error = tc_transact(&request, NULL);
    if (error == EINVAL) {
        /* EINVAL probably means that the default qdisc was in use, in which
         * case we've accomplished our purpose. */
        error = 0;
    }
    if (!error && netdev->tc) {
        if (netdev->tc->ops->tc_destroy) {
            netdev->tc->ops->tc_destroy(netdev->tc);
        }
        netdev->tc = NULL;
    }
    return error;
}

/* If 'netdev''s qdisc type and parameters are not yet known, queries the
 * kernel to determine what they are.  Returns 0 if successful, otherwise a
 * positive errno value. */
static int
tc_query_qdisc(const struct netdev *netdev_)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);
    struct ofpbuf request, *qdisc;
    const struct tc_ops *ops;
    struct tcmsg *tcmsg;
    int load_error;
    int error;

    if (netdev->tc) {
        return 0;
    }

    /* This RTM_GETQDISC is crafted to avoid OOPSing kernels that do not have
     * commit 53b0f08 "net_sched: Fix qdisc_notify()", which is anything before
     * 2.6.35 without that fix backported to it.
     *
     * To avoid the OOPS, we must not make a request that would attempt to dump
     * a "built-in" qdisc, that is, the default pfifo_fast qdisc or one of a
     * few others.  There are a few ways that I can see to do this, but most of
     * them seem to be racy (and if you lose the race the kernel OOPSes).  The
     * technique chosen here is to assume that any non-default qdisc that we
     * create will have a class with handle 1:0.  The built-in qdiscs only have
     * a class with handle 0:0.
     *
     * We could check for Linux 2.6.35+ and use a more straightforward method
     * there. */
    tcmsg = tc_make_request(netdev_, RTM_GETQDISC, NLM_F_ECHO, &request);
    if (!tcmsg) {
        return ENODEV;
    }
    tcmsg->tcm_handle = tc_make_handle(1, 0);
    tcmsg->tcm_parent = 0;

    /* Figure out what tc class to instantiate. */
    error = tc_transact(&request, &qdisc);
    if (!error) {
        const char *kind;

        error = tc_parse_qdisc(qdisc, &kind, NULL);
        if (error) {
            ops = &tc_ops_other;
        } else {
            ops = tc_lookup_linux_name(kind);
            if (!ops) {
                static struct vlog_rate_limit rl2 = VLOG_RATE_LIMIT_INIT(1, 1);
                VLOG_INFO_RL(&rl2, "unknown qdisc \"%s\"", kind);

                ops = &tc_ops_other;
            }
        }
    } else if (error == ENOENT) {
        /* Either it's a built-in qdisc, or it's a qdisc set up by some
         * other entity that doesn't have a handle 1:0.  We will assume
         * that it's the system default qdisc. */
        ops = &tc_ops_default;
        error = 0;
    } else {
        /* Who knows?  Maybe the device got deleted. */
        VLOG_WARN_RL(&rl, "query %s qdisc failed (%s)",
                     netdev_get_name(netdev_), ovs_strerror(error));
        ops = &tc_ops_other;
    }

    /* Instantiate it. */
    load_error = ops->tc_load(CONST_CAST(struct netdev *, netdev_), qdisc);
    ovs_assert((load_error == 0) == (netdev->tc != NULL));
    ofpbuf_delete(qdisc);

    return error ? error : load_error;
}

/* Linux traffic control uses tables with 256 entries ("rtab" tables) to
   approximate the time to transmit packets of various lengths.  For an MTU of
   256 or less, each entry is exact; for an MTU of 257 through 512, each entry
   represents two possible packet lengths; for a MTU of 513 through 1024, four
   possible lengths; and so on.

   Returns, for the specified 'mtu', the number of bits that packet lengths
   need to be shifted right to fit within such a 256-entry table. */
static int
tc_calc_cell_log(unsigned int mtu)
{
    int cell_log;

    if (!mtu) {
        mtu = ETH_PAYLOAD_MAX;
    }
    mtu += ETH_HEADER_LEN + VLAN_HEADER_LEN;

    for (cell_log = 0; mtu >= 256; cell_log++) {
        mtu >>= 1;
    }

    return cell_log;
}

/* Initializes 'rate' properly for a rate of 'Bps' bytes per second with an MTU
 * of 'mtu'. */
static void
tc_fill_rate(struct tc_ratespec *rate, uint64_t Bps, int mtu)
{
    memset(rate, 0, sizeof *rate);
    rate->cell_log = tc_calc_cell_log(mtu);
    /* rate->overhead = 0; */           /* New in 2.6.24, not yet in some */
    /* rate->cell_align = 0; */         /* distro headers. */
    rate->mpu = ETH_TOTAL_MIN;
    rate->rate = Bps;
}

/* Appends to 'msg' an "rtab" table for the specified 'rate' as a Netlink
 * attribute of the specified "type".
 *
 * See tc_calc_cell_log() above for a description of "rtab"s. */
static void
tc_put_rtab(struct ofpbuf *msg, uint16_t type, const struct tc_ratespec *rate)
{
    uint32_t *rtab;
    unsigned int i;

    rtab = nl_msg_put_unspec_uninit(msg, type, TC_RTAB_SIZE);
    for (i = 0; i < TC_RTAB_SIZE / sizeof *rtab; i++) {
        unsigned packet_size = (i + 1) << rate->cell_log;
        if (packet_size < rate->mpu) {
            packet_size = rate->mpu;
        }
        rtab[i] = tc_bytes_to_ticks(rate->rate, packet_size);
    }
}

/* Calculates the proper value of 'buffer' or 'cbuffer' in HTB options given a
 * rate of 'Bps' bytes per second, the specified 'mtu', and a user-requested
 * burst size of 'burst_bytes'.  (If no value was requested, a 'burst_bytes' of
 * 0 is fine.) */
static int
tc_calc_buffer(unsigned int Bps, int mtu, uint64_t burst_bytes)
{
    unsigned int min_burst = tc_buffer_per_jiffy(Bps) + mtu;
    return tc_bytes_to_ticks(Bps, MAX(burst_bytes, min_burst));
}

/* Linux-only functions declared in netdev-linux.h  */

/* Modifies the 'flag' bit in ethtool's flags field for 'netdev'.  If
 * 'enable' is true, the bit is set.  Otherwise, it is cleared. */
int
netdev_linux_ethtool_set_flag(struct netdev *netdev, uint32_t flag,
                              const char *flag_name, bool enable)
{
    const char *netdev_name = netdev_get_name(netdev);
    struct ethtool_value evalue;
    uint32_t new_flags;
    int error;

    COVERAGE_INC(netdev_get_ethtool);
    memset(&evalue, 0, sizeof evalue);
    error = netdev_linux_do_ethtool(netdev_name,
                                    (struct ethtool_cmd *)&evalue,
                                    ETHTOOL_GFLAGS, "ETHTOOL_GFLAGS");
    if (error) {
        return error;
    }

    COVERAGE_INC(netdev_set_ethtool);
    evalue.data = new_flags = (evalue.data & ~flag) | (enable ? flag : 0);
    error = netdev_linux_do_ethtool(netdev_name,
                                    (struct ethtool_cmd *)&evalue,
                                    ETHTOOL_SFLAGS, "ETHTOOL_SFLAGS");
    if (error) {
        return error;
    }

    COVERAGE_INC(netdev_get_ethtool);
    memset(&evalue, 0, sizeof evalue);
    error = netdev_linux_do_ethtool(netdev_name,
                                    (struct ethtool_cmd *)&evalue,
                                    ETHTOOL_GFLAGS, "ETHTOOL_GFLAGS");
    if (error) {
        return error;
    }

    if (new_flags != evalue.data) {
        VLOG_WARN_RL(&rl, "attempt to %s ethtool %s flag on network "
                     "device %s failed", enable ? "enable" : "disable",
                     flag_name, netdev_name);
        return EOPNOTSUPP;
    }

    return 0;
}

/* Utility functions. */

/* Copies 'src' into 'dst', performing format conversion in the process. */
static void
netdev_stats_from_rtnl_link_stats(struct netdev_stats *dst,
                                  const struct rtnl_link_stats *src)
{
    dst->rx_packets = src->rx_packets;
    dst->tx_packets = src->tx_packets;
    dst->rx_bytes = src->rx_bytes;
    dst->tx_bytes = src->tx_bytes;
    dst->rx_errors = src->rx_errors;
    dst->tx_errors = src->tx_errors;
    dst->rx_dropped = src->rx_dropped;
    dst->tx_dropped = src->tx_dropped;
    dst->multicast = src->multicast;
    dst->collisions = src->collisions;
    dst->rx_length_errors = src->rx_length_errors;
    dst->rx_over_errors = src->rx_over_errors;
    dst->rx_crc_errors = src->rx_crc_errors;
    dst->rx_frame_errors = src->rx_frame_errors;
    dst->rx_fifo_errors = src->rx_fifo_errors;
    dst->rx_missed_errors = src->rx_missed_errors;
    dst->tx_aborted_errors = src->tx_aborted_errors;
    dst->tx_carrier_errors = src->tx_carrier_errors;
    dst->tx_fifo_errors = src->tx_fifo_errors;
    dst->tx_heartbeat_errors = src->tx_heartbeat_errors;
    dst->tx_window_errors = src->tx_window_errors;
}

static int
get_stats_via_netlink(int ifindex, struct netdev_stats *stats)
{
    /* Policy for RTNLGRP_LINK messages.
     *
     * There are *many* more fields in these messages, but currently we only
     * care about these fields. */
    static const struct nl_policy rtnlgrp_link_policy[] = {
        [IFLA_IFNAME] = { .type = NL_A_STRING, .optional = false },
        [IFLA_STATS] = { .type = NL_A_UNSPEC, .optional = true,
                         .min_len = sizeof(struct rtnl_link_stats) },
    };

    struct ofpbuf request;
    struct ofpbuf *reply;
    struct ifinfomsg *ifi;
    struct nlattr *attrs[ARRAY_SIZE(rtnlgrp_link_policy)];
    int error;

    ofpbuf_init(&request, 0);
    nl_msg_put_nlmsghdr(&request, sizeof *ifi, RTM_GETLINK, NLM_F_REQUEST);
    ifi = ofpbuf_put_zeros(&request, sizeof *ifi);
    ifi->ifi_family = PF_UNSPEC;
    ifi->ifi_index = ifindex;
    error = nl_transact(NETLINK_ROUTE, &request, &reply);
    ofpbuf_uninit(&request);
    if (error) {
        return error;
    }

    if (!nl_policy_parse(reply, NLMSG_HDRLEN + sizeof(struct ifinfomsg),
                         rtnlgrp_link_policy,
                         attrs, ARRAY_SIZE(rtnlgrp_link_policy))) {
        ofpbuf_delete(reply);
        return EPROTO;
    }

    if (!attrs[IFLA_STATS]) {
        VLOG_WARN_RL(&rl, "RTM_GETLINK reply lacks stats");
        ofpbuf_delete(reply);
        return EPROTO;
    }

    netdev_stats_from_rtnl_link_stats(stats, nl_attr_get(attrs[IFLA_STATS]));

    ofpbuf_delete(reply);

    return 0;
}

static int
get_stats_via_proc(const char *netdev_name, struct netdev_stats *stats)
{
    static const char fn[] = "/proc/net/dev";
    char line[1024];
    FILE *stream;
    int ln;

    stream = fopen(fn, "r");
    if (!stream) {
        VLOG_WARN_RL(&rl, "%s: open failed: %s", fn, ovs_strerror(errno));
        return errno;
    }

    ln = 0;
    while (fgets(line, sizeof line, stream)) {
        if (++ln >= 3) {
            char devname[16];
#define X64 "%"SCNu64
            if (sscanf(line,
                       " %15[^:]:"
                       X64 X64 X64 X64 X64 X64 X64 "%*u"
                       X64 X64 X64 X64 X64 X64 X64 "%*u",
                       devname,
                       &stats->rx_bytes,
                       &stats->rx_packets,
                       &stats->rx_errors,
                       &stats->rx_dropped,
                       &stats->rx_fifo_errors,
                       &stats->rx_frame_errors,
                       &stats->multicast,
                       &stats->tx_bytes,
                       &stats->tx_packets,
                       &stats->tx_errors,
                       &stats->tx_dropped,
                       &stats->tx_fifo_errors,
                       &stats->collisions,
                       &stats->tx_carrier_errors) != 15) {
                VLOG_WARN_RL(&rl, "%s:%d: parse error", fn, ln);
            } else if (!strcmp(devname, netdev_name)) {
                stats->rx_length_errors = UINT64_MAX;
                stats->rx_over_errors = UINT64_MAX;
                stats->rx_crc_errors = UINT64_MAX;
                stats->rx_missed_errors = UINT64_MAX;
                stats->tx_aborted_errors = UINT64_MAX;
                stats->tx_heartbeat_errors = UINT64_MAX;
                stats->tx_window_errors = UINT64_MAX;
                fclose(stream);
                return 0;
            }
        }
    }
    VLOG_WARN_RL(&rl, "%s: no stats for %s", fn, netdev_name);
    fclose(stream);
    return ENODEV;
}

static int
get_flags(const struct netdev *dev, unsigned int *flags)
{
    struct ifreq ifr;
    int error;

    *flags = 0;
    error = af_inet_ifreq_ioctl(dev->name, &ifr, SIOCGIFFLAGS, "SIOCGIFFLAGS");
    if (!error) {
        *flags = ifr.ifr_flags;
    }
    return error;
}

static int
set_flags(const char *name, unsigned int flags)
{
    struct ifreq ifr;

    ifr.ifr_flags = flags;
    return af_inet_ifreq_ioctl(name, &ifr, SIOCSIFFLAGS, "SIOCSIFFLAGS");
}

static int
do_get_ifindex(const char *netdev_name)
{
    struct ifreq ifr;
    int error;

    ovs_strzcpy(ifr.ifr_name, netdev_name, sizeof ifr.ifr_name);
    COVERAGE_INC(netdev_get_ifindex);

    error = af_inet_ioctl(SIOCGIFINDEX, &ifr);
    if (error) {
        VLOG_WARN_RL(&rl, "ioctl(SIOCGIFINDEX) on %s device failed: %s",
                     netdev_name, ovs_strerror(error));
        return -error;
    }
    return ifr.ifr_ifindex;
}

static int
get_ifindex(const struct netdev *netdev_, int *ifindexp)
{
    struct netdev_linux *netdev = netdev_linux_cast(netdev_);

    if (!(netdev->cache_valid & VALID_IFINDEX)) {
        int ifindex = do_get_ifindex(netdev_get_name(netdev_));

        if (ifindex < 0) {
            netdev->get_ifindex_error = -ifindex;
            netdev->ifindex = 0;
        } else {
            netdev->get_ifindex_error = 0;
            netdev->ifindex = ifindex;
        }
        netdev->cache_valid |= VALID_IFINDEX;
    }

    *ifindexp = netdev->ifindex;
    return netdev->get_ifindex_error;
}

static int
get_etheraddr(const char *netdev_name, uint8_t ea[ETH_ADDR_LEN])
{
    struct ifreq ifr;
    int hwaddr_family;
    int error;

    memset(&ifr, 0, sizeof ifr);
    ovs_strzcpy(ifr.ifr_name, netdev_name, sizeof ifr.ifr_name);
    COVERAGE_INC(netdev_get_hwaddr);
    error = af_inet_ioctl(SIOCGIFHWADDR, &ifr);
    if (error) {
        /* ENODEV probably means that a vif disappeared asynchronously and
         * hasn't been removed from the database yet, so reduce the log level
         * to INFO for that case. */
        VLOG(error == ENODEV ? VLL_INFO : VLL_ERR,
             "ioctl(SIOCGIFHWADDR) on %s device failed: %s",
             netdev_name, ovs_strerror(error));
        return error;
    }
    hwaddr_family = ifr.ifr_hwaddr.sa_family;
    if (hwaddr_family != AF_UNSPEC && hwaddr_family != ARPHRD_ETHER) {
        VLOG_WARN("%s device has unknown hardware address family %d",
                  netdev_name, hwaddr_family);
    }
    memcpy(ea, ifr.ifr_hwaddr.sa_data, ETH_ADDR_LEN);
    return 0;
}

static int
set_etheraddr(const char *netdev_name,
              const uint8_t mac[ETH_ADDR_LEN])
{
    struct ifreq ifr;
    int error;

    memset(&ifr, 0, sizeof ifr);
    ovs_strzcpy(ifr.ifr_name, netdev_name, sizeof ifr.ifr_name);
    ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
    memcpy(ifr.ifr_hwaddr.sa_data, mac, ETH_ADDR_LEN);
    COVERAGE_INC(netdev_set_hwaddr);
    error = af_inet_ioctl(SIOCSIFHWADDR, &ifr);
    if (error) {
        VLOG_ERR("ioctl(SIOCSIFHWADDR) on %s device failed: %s",
                 netdev_name, ovs_strerror(error));
    }
    return error;
}

static int
netdev_linux_do_ethtool(const char *name, struct ethtool_cmd *ecmd,
                        int cmd, const char *cmd_name)
{
    struct ifreq ifr;
    int error;

    memset(&ifr, 0, sizeof ifr);
    ovs_strzcpy(ifr.ifr_name, name, sizeof ifr.ifr_name);
    ifr.ifr_data = (caddr_t) ecmd;

    ecmd->cmd = cmd;
    error = af_inet_ioctl(SIOCETHTOOL, &ifr);
    if (error) {
        if (error != EOPNOTSUPP) {
            VLOG_WARN_RL(&rl, "ethtool command %s on network device %s "
                         "failed: %s", cmd_name, name, ovs_strerror(error));
        } else {
            /* The device doesn't support this operation.  That's pretty
             * common, so there's no point in logging anything. */
        }
    }
    return error;
}

static int
netdev_linux_get_ipv4(const struct netdev *netdev, struct in_addr *ip,
                      int cmd, const char *cmd_name)
{
    struct ifreq ifr;
    int error;

    ifr.ifr_addr.sa_family = AF_INET;
    error = af_inet_ifreq_ioctl(netdev_get_name(netdev), &ifr, cmd, cmd_name);
    if (!error) {
        const struct sockaddr_in *sin = ALIGNED_CAST(struct sockaddr_in *,
                                                     &ifr.ifr_addr);
        *ip = sin->sin_addr;
    }
    return error;
}

/* Returns an AF_PACKET raw socket or a negative errno value. */
static int
af_packet_sock(void)
{
    static struct ovsthread_once once = OVSTHREAD_ONCE_INITIALIZER;
    static int sock;

    if (ovsthread_once_start(&once)) {
        sock = socket(AF_PACKET, SOCK_RAW, 0);
        if (sock >= 0) {
            int error = set_nonblocking(sock);
            if (error) {
                close(sock);
                sock = -error;
            }
        } else {
            sock = -errno;
            VLOG_ERR("failed to create packet socket: %s",
                     ovs_strerror(errno));
        }
        ovsthread_once_done(&once);
    }

    return sock;
}
