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
#include <arpa/inet.h>
#include "odp-util.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <netinet/in.h>
#include <netinet/icmp6.h>
#include <stdlib.h>
#include <string.h>
#include "byte-order.h"
#include "coverage.h"
#include "dynamic-string.h"
#include "flow.h"
#include "netlink.h"
#include "ofpbuf.h"
#include "packets.h"
#include "simap.h"
#include "timeval.h"
#include "util.h"
#include "vlog.h"

VLOG_DEFINE_THIS_MODULE(odp_util);

/* The interface between userspace and kernel uses an "OVS_*" prefix.
 * Since this is fairly non-specific for the OVS userspace components,
 * "ODP_*" (Open vSwitch Datapath) is used as the prefix for
 * interactions with the datapath.
 */

/* The set of characters that may separate one action or one key attribute
 * from another. */
static const char *delimiters = ", \t\r\n";

static int parse_odp_key_mask_attr(const char *, const struct simap *port_names,
                              struct ofpbuf *, struct ofpbuf *);
static void format_odp_key_attr(const struct nlattr *a,
                                const struct nlattr *ma, struct ds *ds,
                                bool verbose);

/* Returns one the following for the action with the given OVS_ACTION_ATTR_*
 * 'type':
 *
 *   - For an action whose argument has a fixed length, returned that
 *     nonnegative length in bytes.
 *
 *   - For an action with a variable-length argument, returns -2.
 *
 *   - For an invalid 'type', returns -1. */
static int
odp_action_len(uint16_t type)
{
    if (type > OVS_ACTION_ATTR_MAX) {
        return -1;
    }

    switch ((enum ovs_action_attr) type) {
    case OVS_ACTION_ATTR_OUTPUT: return sizeof(uint32_t);
    case OVS_ACTION_ATTR_USERSPACE: return -2;
    case OVS_ACTION_ATTR_PUSH_VLAN: return sizeof(struct ovs_action_push_vlan);
    case OVS_ACTION_ATTR_POP_VLAN: return 0;
    case OVS_ACTION_ATTR_PUSH_MPLS: return sizeof(struct ovs_action_push_mpls);
    case OVS_ACTION_ATTR_POP_MPLS: return sizeof(ovs_be16);
    case OVS_ACTION_ATTR_SET: return -2;
    case OVS_ACTION_ATTR_SAMPLE: return -2;

    case OVS_ACTION_ATTR_UNSPEC:
    case __OVS_ACTION_ATTR_MAX:
        return -1;
    }

    return -1;
}

/* Returns a string form of 'attr'.  The return value is either a statically
 * allocated constant string or the 'bufsize'-byte buffer 'namebuf'.  'bufsize'
 * should be at least OVS_KEY_ATTR_BUFSIZE. */
enum { OVS_KEY_ATTR_BUFSIZE = 3 + INT_STRLEN(unsigned int) + 1 };
static const char *
ovs_key_attr_to_string(enum ovs_key_attr attr, char *namebuf, size_t bufsize)
{
    switch (attr) {
    case OVS_KEY_ATTR_UNSPEC: return "unspec";
    case OVS_KEY_ATTR_ENCAP: return "encap";
    case OVS_KEY_ATTR_PRIORITY: return "skb_priority";
    case OVS_KEY_ATTR_SKB_MARK: return "skb_mark";
    case OVS_KEY_ATTR_TUNNEL: return "tunnel";
    case OVS_KEY_ATTR_IN_PORT: return "in_port";
    case OVS_KEY_ATTR_ETHERNET: return "eth";
    case OVS_KEY_ATTR_VLAN: return "vlan";
    case OVS_KEY_ATTR_ETHERTYPE: return "eth_type";
    case OVS_KEY_ATTR_IPV4: return "ipv4";
    case OVS_KEY_ATTR_IPV6: return "ipv6";
    case OVS_KEY_ATTR_TCP: return "tcp";
    case OVS_KEY_ATTR_UDP: return "udp";
    case OVS_KEY_ATTR_SCTP: return "sctp";
    case OVS_KEY_ATTR_ICMP: return "icmp";
    case OVS_KEY_ATTR_ICMPV6: return "icmpv6";
    case OVS_KEY_ATTR_ARP: return "arp";
    case OVS_KEY_ATTR_ND: return "nd";
    case OVS_KEY_ATTR_MPLS: return "mpls";

    case __OVS_KEY_ATTR_MAX:
    default:
        snprintf(namebuf, bufsize, "key%u", (unsigned int) attr);
        return namebuf;
    }
}

static void
format_generic_odp_action(struct ds *ds, const struct nlattr *a)
{
    size_t len = nl_attr_get_size(a);

    ds_put_format(ds, "action%"PRId16, nl_attr_type(a));
    if (len) {
        const uint8_t *unspec;
        unsigned int i;

        unspec = nl_attr_get(a);
        for (i = 0; i < len; i++) {
            ds_put_char(ds, i ? ' ': '(');
            ds_put_format(ds, "%02x", unspec[i]);
        }
        ds_put_char(ds, ')');
    }
}

static void
format_odp_sample_action(struct ds *ds, const struct nlattr *attr)
{
    static const struct nl_policy ovs_sample_policy[] = {
        { NL_A_NO_ATTR, 0, 0, false }, /* OVS_SAMPLE_ATTR_UNSPEC */
        { NL_A_U32, 0, 0, false },     /* OVS_SAMPLE_ATTR_PROBABILITY */
        { NL_A_NESTED, 0, 0, false },  /* OVS_SAMPLE_ATTR_ACTIONS */
    };
    struct nlattr *a[ARRAY_SIZE(ovs_sample_policy)];
    double percentage;
    const struct nlattr *nla_acts;
    int len;

    ds_put_cstr(ds, "sample");

    if (!nl_parse_nested(attr, ovs_sample_policy, a, ARRAY_SIZE(a))) {
        ds_put_cstr(ds, "(error)");
        return;
    }

    percentage = (100.0 * nl_attr_get_u32(a[OVS_SAMPLE_ATTR_PROBABILITY])) /
                        UINT32_MAX;

    ds_put_format(ds, "(sample=%.1f%%,", percentage);

    ds_put_cstr(ds, "actions(");
    nla_acts = nl_attr_get(a[OVS_SAMPLE_ATTR_ACTIONS]);
    len = nl_attr_get_size(a[OVS_SAMPLE_ATTR_ACTIONS]);
    format_odp_actions(ds, nla_acts, len);
    ds_put_format(ds, "))");
}

static const char *
slow_path_reason_to_string(enum slow_path_reason reason)
{
    switch (reason) {
    case SLOW_CFM:
        return "cfm";
    case SLOW_LACP:
        return "lacp";
    case SLOW_STP:
        return "stp";
    case SLOW_BFD:
        return "bfd";
    case SLOW_CONTROLLER:
        return "controller";
    case __SLOW_MAX:
    default:
        return NULL;
    }
}

static enum slow_path_reason
string_to_slow_path_reason(const char *string)
{
    enum slow_path_reason i;

    for (i = 1; i < __SLOW_MAX; i++) {
        if (!strcmp(string, slow_path_reason_to_string(i))) {
            return i;
        }
    }

    return 0;
}

static int
parse_flags(const char *s, const char *(*bit_to_string)(uint32_t),
            uint32_t *res)
{
    uint32_t result = 0;
    int n = 0;

    if (s[n] != '(') {
        return -EINVAL;
    }
    n++;

    while (s[n] != ')') {
        unsigned long long int flags;
        uint32_t bit;
        int n0;

        if (sscanf(&s[n], "%lli%n", &flags, &n0) > 0 && n0 > 0) {
            n += n0 + (s[n + n0] == ',');
            result |= flags;
            continue;
        }

        for (bit = 1; bit; bit <<= 1) {
            const char *name = bit_to_string(bit);
            size_t len;

            if (!name) {
                continue;
            }

            len = strlen(name);
            if (!strncmp(s + n, name, len) &&
                (s[n + len] == ',' || s[n + len] == ')')) {
                result |= bit;
                n += len + (s[n + len] == ',');
                break;
            }
        }

        if (!bit) {
            return -EINVAL;
        }
    }
    n++;

    *res = result;
    return n;
}

static void
format_odp_userspace_action(struct ds *ds, const struct nlattr *attr)
{
    static const struct nl_policy ovs_userspace_policy[] = {
        { NL_A_NO_ATTR, 0, 0, false }, /* OVS_USERSPACE_ATTR_UNSPEC */
        { NL_A_U32, 0, 0, false },     /* OVS_USERSPACE_ATTR_PID */
        { NL_A_UNSPEC, 0, 0, true },   /* OVS_USERSPACE_ATTR_USERDATA */
    };
    struct nlattr *a[ARRAY_SIZE(ovs_userspace_policy)];
    const struct nlattr *userdata_attr;

    if (!nl_parse_nested(attr, ovs_userspace_policy, a, ARRAY_SIZE(a))) {
        ds_put_cstr(ds, "userspace(error)");
        return;
    }

    ds_put_format(ds, "userspace(pid=%"PRIu32,
                  nl_attr_get_u32(a[OVS_USERSPACE_ATTR_PID]));

    userdata_attr = a[OVS_USERSPACE_ATTR_USERDATA];

    if (userdata_attr) {
        const uint8_t *userdata = nl_attr_get(userdata_attr);
        size_t userdata_len = nl_attr_get_size(userdata_attr);
        bool userdata_unspec = true;
        union user_action_cookie cookie;

        if (userdata_len >= sizeof cookie.type
            && userdata_len <= sizeof cookie) {

            memset(&cookie, 0, sizeof cookie);
            memcpy(&cookie, userdata, userdata_len);

            userdata_unspec = false;

            if (userdata_len == sizeof cookie.sflow
                && cookie.type == USER_ACTION_COOKIE_SFLOW) {
                ds_put_format(ds, ",sFlow("
                              "vid=%"PRIu16",pcp=%"PRIu8",output=%"PRIu32")",
                              vlan_tci_to_vid(cookie.sflow.vlan_tci),
                              vlan_tci_to_pcp(cookie.sflow.vlan_tci),
                              cookie.sflow.output);
            } else if (userdata_len == sizeof cookie.slow_path
                       && cookie.type == USER_ACTION_COOKIE_SLOW_PATH) {
                const char *reason;
                reason = slow_path_reason_to_string(cookie.slow_path.reason);
                reason = reason ? reason : "";
                ds_put_format(ds, ",slow_path(%s)", reason);
            } else if (userdata_len == sizeof cookie.flow_sample
                       && cookie.type == USER_ACTION_COOKIE_FLOW_SAMPLE) {
                ds_put_format(ds, ",flow_sample(probability=%"PRIu16
                              ",collector_set_id=%"PRIu32
                              ",obs_domain_id=%"PRIu32
                              ",obs_point_id=%"PRIu32")",
                              cookie.flow_sample.probability,
                              cookie.flow_sample.collector_set_id,
                              cookie.flow_sample.obs_domain_id,
                              cookie.flow_sample.obs_point_id);
            } else if (userdata_len == sizeof cookie.ipfix
                       && cookie.type == USER_ACTION_COOKIE_IPFIX) {
                ds_put_format(ds, ",ipfix");
            } else {
                userdata_unspec = true;
            }
        }

        if (userdata_unspec) {
            size_t i;
            ds_put_format(ds, ",userdata(");
            for (i = 0; i < userdata_len; i++) {
                ds_put_format(ds, "%02x", userdata[i]);
            }
            ds_put_char(ds, ')');
        }
    }

    ds_put_char(ds, ')');
}

static void
format_vlan_tci(struct ds *ds, ovs_be16 vlan_tci)
{
    ds_put_format(ds, "vid=%"PRIu16",pcp=%d",
                  vlan_tci_to_vid(vlan_tci),
                  vlan_tci_to_pcp(vlan_tci));
    if (!(vlan_tci & htons(VLAN_CFI))) {
        ds_put_cstr(ds, ",cfi=0");
    }
}

static void
format_mpls_lse(struct ds *ds, ovs_be32 mpls_lse)
{
    ds_put_format(ds, "label=%"PRIu32",tc=%d,ttl=%d,bos=%d",
                  mpls_lse_to_label(mpls_lse),
                  mpls_lse_to_tc(mpls_lse),
                  mpls_lse_to_ttl(mpls_lse),
                  mpls_lse_to_bos(mpls_lse));
}

static void
format_mpls(struct ds *ds, const struct ovs_key_mpls *mpls_key,
            const struct ovs_key_mpls *mpls_mask)
{
    ovs_be32 key = mpls_key->mpls_lse;

    if (mpls_mask == NULL) {
        format_mpls_lse(ds, key);
    } else {
        ovs_be32 mask = mpls_mask->mpls_lse;

        ds_put_format(ds, "label=%"PRIu32"/0x%x,tc=%d/%x,ttl=%d/0x%x,bos=%d/%x",
                  mpls_lse_to_label(key), mpls_lse_to_label(mask),
                  mpls_lse_to_tc(key), mpls_lse_to_tc(mask),
                  mpls_lse_to_ttl(key), mpls_lse_to_ttl(mask),
                  mpls_lse_to_bos(key), mpls_lse_to_bos(mask));
    }
}

static void
format_odp_action(struct ds *ds, const struct nlattr *a)
{
    int expected_len;
    enum ovs_action_attr type = nl_attr_type(a);
    const struct ovs_action_push_vlan *vlan;

    expected_len = odp_action_len(nl_attr_type(a));
    if (expected_len != -2 && nl_attr_get_size(a) != expected_len) {
        ds_put_format(ds, "bad length %zu, expected %d for: ",
                      nl_attr_get_size(a), expected_len);
        format_generic_odp_action(ds, a);
        return;
    }

    switch (type) {
    case OVS_ACTION_ATTR_OUTPUT:
        ds_put_format(ds, "%"PRIu32, nl_attr_get_u32(a));
        break;
    case OVS_ACTION_ATTR_USERSPACE:
        format_odp_userspace_action(ds, a);
        break;
    case OVS_ACTION_ATTR_SET:
        ds_put_cstr(ds, "set(");
        format_odp_key_attr(nl_attr_get(a), NULL, ds, true);
        ds_put_cstr(ds, ")");
        break;
    case OVS_ACTION_ATTR_PUSH_VLAN:
        vlan = nl_attr_get(a);
        ds_put_cstr(ds, "push_vlan(");
        if (vlan->vlan_tpid != htons(ETH_TYPE_VLAN)) {
            ds_put_format(ds, "tpid=0x%04"PRIx16",", ntohs(vlan->vlan_tpid));
        }
        format_vlan_tci(ds, vlan->vlan_tci);
        ds_put_char(ds, ')');
        break;
    case OVS_ACTION_ATTR_POP_VLAN:
        ds_put_cstr(ds, "pop_vlan");
        break;
    case OVS_ACTION_ATTR_PUSH_MPLS: {
        const struct ovs_action_push_mpls *mpls = nl_attr_get(a);
        ds_put_cstr(ds, "push_mpls(");
        format_mpls_lse(ds, mpls->mpls_lse);
        ds_put_format(ds, ",eth_type=0x%"PRIx16")", ntohs(mpls->mpls_ethertype));
        break;
    }
    case OVS_ACTION_ATTR_POP_MPLS: {
        ovs_be16 ethertype = nl_attr_get_be16(a);
        ds_put_format(ds, "pop_mpls(eth_type=0x%"PRIx16")", ntohs(ethertype));
        break;
    }
    case OVS_ACTION_ATTR_SAMPLE:
        format_odp_sample_action(ds, a);
        break;
    case OVS_ACTION_ATTR_UNSPEC:
    case __OVS_ACTION_ATTR_MAX:
    default:
        format_generic_odp_action(ds, a);
        break;
    }
}

void
format_odp_actions(struct ds *ds, const struct nlattr *actions,
                   size_t actions_len)
{
    if (actions_len) {
        const struct nlattr *a;
        unsigned int left;

        NL_ATTR_FOR_EACH (a, left, actions, actions_len) {
            if (a != actions) {
                ds_put_char(ds, ',');
            }
            format_odp_action(ds, a);
        }
        if (left) {
            int i;

            if (left == actions_len) {
                ds_put_cstr(ds, "<empty>");
            }
            ds_put_format(ds, ",***%u leftover bytes*** (", left);
            for (i = 0; i < left; i++) {
                ds_put_format(ds, "%02x", ((const uint8_t *) a)[i]);
            }
            ds_put_char(ds, ')');
        }
    } else {
        ds_put_cstr(ds, "drop");
    }
}

static int
parse_odp_action(const char *s, const struct simap *port_names,
                 struct ofpbuf *actions)
{
    /* Many of the sscanf calls in this function use oversized destination
     * fields because some sscanf() implementations truncate the range of %i
     * directives, so that e.g. "%"SCNi16 interprets input of "0xfedc" as a
     * value of 0x7fff.  The other alternatives are to allow only a single
     * radix (e.g. decimal or hexadecimal) or to write more sophisticated
     * parsers.
     *
     * The tun_id parser has to use an alternative approach because there is no
     * type larger than 64 bits. */

    {
        unsigned long long int port;
        int n = -1;

        if (sscanf(s, "%lli%n", &port, &n) > 0 && n > 0) {
            nl_msg_put_u32(actions, OVS_ACTION_ATTR_OUTPUT, port);
            return n;
        }
    }

    if (port_names) {
        int len = strcspn(s, delimiters);
        struct simap_node *node;

        node = simap_find_len(port_names, s, len);
        if (node) {
            nl_msg_put_u32(actions, OVS_ACTION_ATTR_OUTPUT, node->data);
            return len;
        }
    }

    {
        unsigned long long int pid;
        unsigned long long int output;
        unsigned long long int probability;
        unsigned long long int collector_set_id;
        unsigned long long int obs_domain_id;
        unsigned long long int obs_point_id;
        int vid, pcp;
        int n = -1;

        if (sscanf(s, "userspace(pid=%lli)%n", &pid, &n) > 0 && n > 0) {
            odp_put_userspace_action(pid, NULL, 0, actions);
            return n;
        } else if (sscanf(s, "userspace(pid=%lli,sFlow(vid=%i,"
                          "pcp=%i,output=%lli))%n",
                          &pid, &vid, &pcp, &output, &n) > 0 && n > 0) {
            union user_action_cookie cookie;
            uint16_t tci;

            tci = vid | (pcp << VLAN_PCP_SHIFT);
            if (tci) {
                tci |= VLAN_CFI;
            }

            cookie.type = USER_ACTION_COOKIE_SFLOW;
            cookie.sflow.vlan_tci = htons(tci);
            cookie.sflow.output = output;
            odp_put_userspace_action(pid, &cookie, sizeof cookie.sflow,
                                     actions);
            return n;
        } else if (sscanf(s, "userspace(pid=%lli,slow_path(%n", &pid, &n) > 0
                   && n > 0) {
            union user_action_cookie cookie;
            char reason[32];

            if (s[n] == ')' && s[n + 1] == ')') {
                reason[0] = '\0';
                n += 2;
            } else if (sscanf(s + n, "%31[^)]))", reason) > 0) {
                n += strlen(reason) + 2;
            } else {
                return -EINVAL;
            }

            cookie.type = USER_ACTION_COOKIE_SLOW_PATH;
            cookie.slow_path.unused = 0;
            cookie.slow_path.reason = string_to_slow_path_reason(reason);

            if (reason[0] && !cookie.slow_path.reason) {
                return -EINVAL;
            }

            odp_put_userspace_action(pid, &cookie, sizeof cookie.slow_path,
                                     actions);
            return n;
        } else if (sscanf(s, "userspace(pid=%lli,flow_sample(probability=%lli,"
                          "collector_set_id=%lli,obs_domain_id=%lli,"
                          "obs_point_id=%lli))%n",
                          &pid, &probability, &collector_set_id,
                          &obs_domain_id, &obs_point_id, &n) > 0 && n > 0) {
            union user_action_cookie cookie;

            cookie.type = USER_ACTION_COOKIE_FLOW_SAMPLE;
            cookie.flow_sample.probability = probability;
            cookie.flow_sample.collector_set_id = collector_set_id;
            cookie.flow_sample.obs_domain_id = obs_domain_id;
            cookie.flow_sample.obs_point_id = obs_point_id;
            odp_put_userspace_action(pid, &cookie, sizeof cookie.flow_sample,
                                     actions);
            return n;
        } else if (sscanf(s, "userspace(pid=%lli,ipfix)%n", &pid, &n) > 0
                   && n > 0) {
            union user_action_cookie cookie;

            cookie.type = USER_ACTION_COOKIE_IPFIX;
            odp_put_userspace_action(pid, &cookie, sizeof cookie.ipfix,
                                     actions);
            return n;
        } else if (sscanf(s, "userspace(pid=%lli,userdata(%n", &pid, &n) > 0
                   && n > 0) {
            struct ofpbuf buf;
            char *end;

            ofpbuf_init(&buf, 16);
            end = ofpbuf_put_hex(&buf, &s[n], NULL);
            if (end[0] == ')' && end[1] == ')') {
                odp_put_userspace_action(pid, buf.data, buf.size, actions);
                ofpbuf_uninit(&buf);
                return (end + 2) - s;
            }
        }
    }

    if (!strncmp(s, "set(", 4)) {
        size_t start_ofs;
        int retval;

        start_ofs = nl_msg_start_nested(actions, OVS_ACTION_ATTR_SET);
        retval = parse_odp_key_mask_attr(s + 4, port_names, actions, NULL);
        if (retval < 0) {
            return retval;
        }
        if (s[retval + 4] != ')') {
            return -EINVAL;
        }
        nl_msg_end_nested(actions, start_ofs);
        return retval + 5;
    }

    {
        struct ovs_action_push_vlan push;
        int tpid = ETH_TYPE_VLAN;
        int vid, pcp;
        int cfi = 1;
        int n = -1;

        if ((sscanf(s, "push_vlan(vid=%i,pcp=%i)%n", &vid, &pcp, &n) > 0
             && n > 0)
            || (sscanf(s, "push_vlan(vid=%i,pcp=%i,cfi=%i)%n",
                       &vid, &pcp, &cfi, &n) > 0 && n > 0)
            || (sscanf(s, "push_vlan(tpid=%i,vid=%i,pcp=%i)%n",
                       &tpid, &vid, &pcp, &n) > 0 && n > 0)
            || (sscanf(s, "push_vlan(tpid=%i,vid=%i,pcp=%i,cfi=%i)%n",
                       &tpid, &vid, &pcp, &cfi, &n) > 0 && n > 0)) {
            push.vlan_tpid = htons(tpid);
            push.vlan_tci = htons((vid << VLAN_VID_SHIFT)
                                  | (pcp << VLAN_PCP_SHIFT)
                                  | (cfi ? VLAN_CFI : 0));
            nl_msg_put_unspec(actions, OVS_ACTION_ATTR_PUSH_VLAN,
                              &push, sizeof push);

            return n;
        }
    }

    if (!strncmp(s, "pop_vlan", 8)) {
        nl_msg_put_flag(actions, OVS_ACTION_ATTR_POP_VLAN);
        return 8;
    }

    {
        double percentage;
        int n = -1;

        if (sscanf(s, "sample(sample=%lf%%,actions(%n", &percentage, &n) > 0
            && percentage >= 0. && percentage <= 100.0
            && n > 0) {
            size_t sample_ofs, actions_ofs;
            double probability;

            probability = floor(UINT32_MAX * (percentage / 100.0) + .5);
            sample_ofs = nl_msg_start_nested(actions, OVS_ACTION_ATTR_SAMPLE);
            nl_msg_put_u32(actions, OVS_SAMPLE_ATTR_PROBABILITY,
                           (probability <= 0 ? 0
                            : probability >= UINT32_MAX ? UINT32_MAX
                            : probability));

            actions_ofs = nl_msg_start_nested(actions,
                                              OVS_SAMPLE_ATTR_ACTIONS);
            for (;;) {
                int retval;

                n += strspn(s + n, delimiters);
                if (s[n] == ')') {
                    break;
                }

                retval = parse_odp_action(s + n, port_names, actions);
                if (retval < 0) {
                    return retval;
                }
                n += retval;
            }
            nl_msg_end_nested(actions, actions_ofs);
            nl_msg_end_nested(actions, sample_ofs);

            return s[n + 1] == ')' ? n + 2 : -EINVAL;
        }
    }

    return -EINVAL;
}

/* Parses the string representation of datapath actions, in the format output
 * by format_odp_action().  Returns 0 if successful, otherwise a positive errno
 * value.  On success, the ODP actions are appended to 'actions' as a series of
 * Netlink attributes.  On failure, no data is appended to 'actions'.  Either
 * way, 'actions''s data might be reallocated. */
int
odp_actions_from_string(const char *s, const struct simap *port_names,
                        struct ofpbuf *actions)
{
    size_t old_size;

    if (!strcasecmp(s, "drop")) {
        return 0;
    }

    old_size = actions->size;
    for (;;) {
        int retval;

        s += strspn(s, delimiters);
        if (!*s) {
            return 0;
        }

        retval = parse_odp_action(s, port_names, actions);
        if (retval < 0 || !strchr(delimiters, s[retval])) {
            actions->size = old_size;
            return -retval;
        }
        s += retval;
    }

    return 0;
}

/* Returns the correct length of the payload for a flow key attribute of the
 * specified 'type', -1 if 'type' is unknown, or -2 if the attribute's payload
 * is variable length. */
static int
odp_flow_key_attr_len(uint16_t type)
{
    if (type > OVS_KEY_ATTR_MAX) {
        return -1;
    }

    switch ((enum ovs_key_attr) type) {
    case OVS_KEY_ATTR_ENCAP: return -2;
    case OVS_KEY_ATTR_PRIORITY: return 4;
    case OVS_KEY_ATTR_SKB_MARK: return 4;
    case OVS_KEY_ATTR_TUNNEL: return -2;
    case OVS_KEY_ATTR_IN_PORT: return 4;
    case OVS_KEY_ATTR_ETHERNET: return sizeof(struct ovs_key_ethernet);
    case OVS_KEY_ATTR_VLAN: return sizeof(ovs_be16);
    case OVS_KEY_ATTR_ETHERTYPE: return 2;
    case OVS_KEY_ATTR_MPLS: return sizeof(struct ovs_key_mpls);
    case OVS_KEY_ATTR_IPV4: return sizeof(struct ovs_key_ipv4);
    case OVS_KEY_ATTR_IPV6: return sizeof(struct ovs_key_ipv6);
    case OVS_KEY_ATTR_TCP: return sizeof(struct ovs_key_tcp);
    case OVS_KEY_ATTR_UDP: return sizeof(struct ovs_key_udp);
    case OVS_KEY_ATTR_SCTP: return sizeof(struct ovs_key_sctp);
    case OVS_KEY_ATTR_ICMP: return sizeof(struct ovs_key_icmp);
    case OVS_KEY_ATTR_ICMPV6: return sizeof(struct ovs_key_icmpv6);
    case OVS_KEY_ATTR_ARP: return sizeof(struct ovs_key_arp);
    case OVS_KEY_ATTR_ND: return sizeof(struct ovs_key_nd);

    case OVS_KEY_ATTR_UNSPEC:
    case __OVS_KEY_ATTR_MAX:
        return -1;
    }

    return -1;
}

static void
format_generic_odp_key(const struct nlattr *a, struct ds *ds)
{
    size_t len = nl_attr_get_size(a);
    if (len) {
        const uint8_t *unspec;
        unsigned int i;

        unspec = nl_attr_get(a);
        for (i = 0; i < len; i++) {
            if (i) {
                ds_put_char(ds, ' ');
            }
            ds_put_format(ds, "%02x", unspec[i]);
        }
    }
}

static const char *
ovs_frag_type_to_string(enum ovs_frag_type type)
{
    switch (type) {
    case OVS_FRAG_TYPE_NONE:
        return "no";
    case OVS_FRAG_TYPE_FIRST:
        return "first";
    case OVS_FRAG_TYPE_LATER:
        return "later";
    case __OVS_FRAG_TYPE_MAX:
    default:
        return "<error>";
    }
}

static int
tunnel_key_attr_len(int type)
{
    switch (type) {
    case OVS_TUNNEL_KEY_ATTR_ID: return 8;
    case OVS_TUNNEL_KEY_ATTR_IPV4_SRC: return 4;
    case OVS_TUNNEL_KEY_ATTR_IPV4_DST: return 4;
    case OVS_TUNNEL_KEY_ATTR_TOS: return 1;
    case OVS_TUNNEL_KEY_ATTR_TTL: return 1;
    case OVS_TUNNEL_KEY_ATTR_DONT_FRAGMENT: return 0;
    case OVS_TUNNEL_KEY_ATTR_CSUM: return 0;
    case __OVS_TUNNEL_KEY_ATTR_MAX:
        return -1;
    }
    return -1;
}

enum odp_key_fitness
odp_tun_key_from_attr(const struct nlattr *attr, struct flow_tnl *tun)
{
    unsigned int left;
    const struct nlattr *a;
    bool ttl = false;
    bool unknown = false;

    NL_NESTED_FOR_EACH(a, left, attr) {
        uint16_t type = nl_attr_type(a);
        size_t len = nl_attr_get_size(a);
        int expected_len = tunnel_key_attr_len(type);

        if (len != expected_len && expected_len >= 0) {
            return ODP_FIT_ERROR;
        }

        switch (type) {
        case OVS_TUNNEL_KEY_ATTR_ID:
            tun->tun_id = nl_attr_get_be64(a);
            tun->flags |= FLOW_TNL_F_KEY;
            break;
        case OVS_TUNNEL_KEY_ATTR_IPV4_SRC:
            tun->ip_src = nl_attr_get_be32(a);
            break;
        case OVS_TUNNEL_KEY_ATTR_IPV4_DST:
            tun->ip_dst = nl_attr_get_be32(a);
            break;
        case OVS_TUNNEL_KEY_ATTR_TOS:
            tun->ip_tos = nl_attr_get_u8(a);
            break;
        case OVS_TUNNEL_KEY_ATTR_TTL:
            tun->ip_ttl = nl_attr_get_u8(a);
            ttl = true;
            break;
        case OVS_TUNNEL_KEY_ATTR_DONT_FRAGMENT:
            tun->flags |= FLOW_TNL_F_DONT_FRAGMENT;
            break;
        case OVS_TUNNEL_KEY_ATTR_CSUM:
            tun->flags |= FLOW_TNL_F_CSUM;
            break;
        default:
            /* Allow this to show up as unexpected, if there are unknown
             * tunnel attribute, eventually resulting in ODP_FIT_TOO_MUCH. */
            unknown = true;
            break;
        }
    }

    if (!ttl) {
        return ODP_FIT_ERROR;
    }
    if (unknown) {
            return ODP_FIT_TOO_MUCH;
    }
    return ODP_FIT_PERFECT;
}

static void
tun_key_to_attr(struct ofpbuf *a, const struct flow_tnl *tun_key)
{
    size_t tun_key_ofs;

    tun_key_ofs = nl_msg_start_nested(a, OVS_KEY_ATTR_TUNNEL);

    if (tun_key->flags & FLOW_TNL_F_KEY) {
        nl_msg_put_be64(a, OVS_TUNNEL_KEY_ATTR_ID, tun_key->tun_id);
    }
    if (tun_key->ip_src) {
        nl_msg_put_be32(a, OVS_TUNNEL_KEY_ATTR_IPV4_SRC, tun_key->ip_src);
    }
    if (tun_key->ip_dst) {
        nl_msg_put_be32(a, OVS_TUNNEL_KEY_ATTR_IPV4_DST, tun_key->ip_dst);
    }
    if (tun_key->ip_tos) {
        nl_msg_put_u8(a, OVS_TUNNEL_KEY_ATTR_TOS, tun_key->ip_tos);
    }
    nl_msg_put_u8(a, OVS_TUNNEL_KEY_ATTR_TTL, tun_key->ip_ttl);
    if (tun_key->flags & FLOW_TNL_F_DONT_FRAGMENT) {
        nl_msg_put_flag(a, OVS_TUNNEL_KEY_ATTR_DONT_FRAGMENT);
    }
    if (tun_key->flags & FLOW_TNL_F_CSUM) {
        nl_msg_put_flag(a, OVS_TUNNEL_KEY_ATTR_CSUM);
    }

    nl_msg_end_nested(a, tun_key_ofs);
}

static bool
odp_mask_attr_is_wildcard(const struct nlattr *ma)
{
    return is_all_zeros(nl_attr_get(ma), nl_attr_get_size(ma));
}

static bool
odp_mask_attr_is_exact(const struct nlattr *ma)
{
    bool is_exact = false;
    enum ovs_key_attr attr = nl_attr_type(ma);

    if (attr == OVS_KEY_ATTR_TUNNEL) {
        /* XXX this is a hack for now. Should change
         * the exact match dection to per field
         * instead of per attribute.
         */
        struct flow_tnl tun_mask;
        memset(&tun_mask, 0, sizeof tun_mask);
        odp_tun_key_from_attr(ma, &tun_mask);
        if (tun_mask.flags == (FLOW_TNL_F_KEY
                               | FLOW_TNL_F_DONT_FRAGMENT
                               | FLOW_TNL_F_CSUM)) {
            /* The flags are exact match, check the remaining fields. */
            tun_mask.flags = 0xffff;
            is_exact = is_all_ones((uint8_t *)&tun_mask,
                                   offsetof(struct flow_tnl, ip_ttl));
        }
    } else {
        is_exact = is_all_ones(nl_attr_get(ma), nl_attr_get_size(ma));
    }

    return is_exact;
}


static void
format_odp_key_attr(const struct nlattr *a, const struct nlattr *ma,
                    struct ds *ds, bool verbose)
{
    struct flow_tnl tun_key;
    enum ovs_key_attr attr = nl_attr_type(a);
    char namebuf[OVS_KEY_ATTR_BUFSIZE];
    int expected_len;
    bool is_exact;

    is_exact = ma ? odp_mask_attr_is_exact(ma) : true;

    ds_put_cstr(ds, ovs_key_attr_to_string(attr, namebuf, sizeof namebuf));

    {
        expected_len = odp_flow_key_attr_len(nl_attr_type(a));
        if (expected_len != -2) {
            bool bad_key_len = nl_attr_get_size(a) != expected_len;
            bool bad_mask_len = ma && nl_attr_get_size(a) != expected_len;

            if (bad_key_len || bad_mask_len) {
                if (bad_key_len) {
                    ds_put_format(ds, "(bad key length %zu, expected %d)(",
                                  nl_attr_get_size(a),
                                  odp_flow_key_attr_len(nl_attr_type(a)));
                }
                format_generic_odp_key(a, ds);
                if (bad_mask_len) {
                    ds_put_char(ds, '/');
                    ds_put_format(ds, "(bad mask length %zu, expected %d)(",
                                  nl_attr_get_size(ma),
                                  odp_flow_key_attr_len(nl_attr_type(ma)));
                }
                format_generic_odp_key(ma, ds);
                ds_put_char(ds, ')');
                return;
            }
        }
    }

    ds_put_char(ds, '(');
    switch (attr) {
    case OVS_KEY_ATTR_ENCAP:
        if (ma && nl_attr_get_size(ma) && nl_attr_get_size(a)) {
            odp_flow_format(nl_attr_get(a), nl_attr_get_size(a),
                            nl_attr_get(ma), nl_attr_get_size(ma), ds, verbose);
        } else if (nl_attr_get_size(a)) {
            odp_flow_format(nl_attr_get(a), nl_attr_get_size(a), NULL, 0, ds,
                            verbose);
        }
        break;

    case OVS_KEY_ATTR_PRIORITY:
    case OVS_KEY_ATTR_SKB_MARK:
        ds_put_format(ds, "%#"PRIx32, nl_attr_get_u32(a));
        if (!is_exact) {
            ds_put_format(ds, "/%#"PRIx32, nl_attr_get_u32(ma));
        }
        break;

    case OVS_KEY_ATTR_TUNNEL:
        memset(&tun_key, 0, sizeof tun_key);
        if (odp_tun_key_from_attr(a, &tun_key) == ODP_FIT_ERROR) {
            ds_put_format(ds, "error");
        } else if (!is_exact) {
            struct flow_tnl tun_mask;

            memset(&tun_mask, 0, sizeof tun_mask);
            odp_tun_key_from_attr(ma, &tun_mask);
            ds_put_format(ds, "tun_id=%#"PRIx64"/%#"PRIx64
                          ",src="IP_FMT"/"IP_FMT",dst="IP_FMT"/"IP_FMT
                          ",tos=%#"PRIx8"/%#"PRIx8",ttl=%"PRIu8"/%#"PRIx8
                          ",flags(",
                          ntohll(tun_key.tun_id), ntohll(tun_mask.tun_id),
                          IP_ARGS(tun_key.ip_src), IP_ARGS(tun_mask.ip_src),
                          IP_ARGS(tun_key.ip_dst), IP_ARGS(tun_mask.ip_dst),
                          tun_key.ip_tos, tun_mask.ip_tos,
                          tun_key.ip_ttl, tun_mask.ip_ttl);

            format_flags(ds, flow_tun_flag_to_string, tun_key.flags, ',');

            /* XXX This code is correct, but enabling it would break the unit
               test. Disable it for now until the input parser is fixed.

                ds_put_char(ds, '/');
                format_flags(ds, flow_tun_flag_to_string, tun_mask.flags, ',');
            */
            ds_put_char(ds, ')');
        } else {
            ds_put_format(ds, "tun_id=0x%"PRIx64",src="IP_FMT",dst="IP_FMT","
                          "tos=0x%"PRIx8",ttl=%"PRIu8",flags(",
                          ntohll(tun_key.tun_id),
                          IP_ARGS(tun_key.ip_src),
                          IP_ARGS(tun_key.ip_dst),
                          tun_key.ip_tos, tun_key.ip_ttl);

            format_flags(ds, flow_tun_flag_to_string, tun_key.flags, ',');
            ds_put_char(ds, ')');
        }
        break;

    case OVS_KEY_ATTR_IN_PORT:
        ds_put_format(ds, "%"PRIu32, nl_attr_get_u32(a));
        if (!is_exact) {
            ds_put_format(ds, "/%#"PRIx32, nl_attr_get_u32(ma));
        }
        break;

    case OVS_KEY_ATTR_ETHERNET:
        if (!is_exact) {
            const struct ovs_key_ethernet *eth_mask = nl_attr_get(ma);
            const struct ovs_key_ethernet *eth_key = nl_attr_get(a);

            ds_put_format(ds, "src="ETH_ADDR_FMT"/"ETH_ADDR_FMT
                          ",dst="ETH_ADDR_FMT"/"ETH_ADDR_FMT,
                          ETH_ADDR_ARGS(eth_key->eth_src),
                          ETH_ADDR_ARGS(eth_mask->eth_src),
                          ETH_ADDR_ARGS(eth_key->eth_dst),
                          ETH_ADDR_ARGS(eth_mask->eth_dst));
        } else {
            const struct ovs_key_ethernet *eth_key = nl_attr_get(a);

            ds_put_format(ds, "src="ETH_ADDR_FMT",dst="ETH_ADDR_FMT,
                          ETH_ADDR_ARGS(eth_key->eth_src),
                          ETH_ADDR_ARGS(eth_key->eth_dst));
        }
        break;

    case OVS_KEY_ATTR_VLAN:
        {
            ovs_be16 vlan_tci = nl_attr_get_be16(a);
            if (!is_exact) {
                ovs_be16 mask = nl_attr_get_be16(ma);
                ds_put_format(ds, "vid=%"PRIu16"/0x%"PRIx16",pcp=%d/0x%x,cfi=%d/%d",
                              vlan_tci_to_vid(vlan_tci),
                              vlan_tci_to_vid(mask),
                              vlan_tci_to_pcp(vlan_tci),
                              vlan_tci_to_pcp(mask),
                              vlan_tci_to_cfi(vlan_tci),
                              vlan_tci_to_cfi(mask));
            } else {
                format_vlan_tci(ds, vlan_tci);
            }
        }
        break;

    case OVS_KEY_ATTR_MPLS: {
        const struct ovs_key_mpls *mpls_key = nl_attr_get(a);
        const struct ovs_key_mpls *mpls_mask = NULL;
        if (!is_exact) {
            mpls_mask = nl_attr_get(ma);
        }
        format_mpls(ds, mpls_key, mpls_mask);
        break;
    }

    case OVS_KEY_ATTR_ETHERTYPE:
        ds_put_format(ds, "0x%04"PRIx16, ntohs(nl_attr_get_be16(a)));
        if (!is_exact) {
            ds_put_format(ds, "/0x%04"PRIx16, ntohs(nl_attr_get_be16(ma)));
        }
        break;

    case OVS_KEY_ATTR_IPV4:
        if (!is_exact) {
            const struct ovs_key_ipv4 *ipv4_key = nl_attr_get(a);
            const struct ovs_key_ipv4 *ipv4_mask = nl_attr_get(ma);

            ds_put_format(ds, "src="IP_FMT"/"IP_FMT",dst="IP_FMT"/"IP_FMT
                          ",proto=%"PRIu8"/%#"PRIx8",tos=%#"PRIx8"/%#"PRIx8
                          ",ttl=%"PRIu8"/%#"PRIx8",frag=%s/%#"PRIx8,
                          IP_ARGS(ipv4_key->ipv4_src),
                          IP_ARGS(ipv4_mask->ipv4_src),
                          IP_ARGS(ipv4_key->ipv4_dst),
                          IP_ARGS(ipv4_mask->ipv4_dst),
                          ipv4_key->ipv4_proto, ipv4_mask->ipv4_proto,
                          ipv4_key->ipv4_tos, ipv4_mask->ipv4_tos,
                          ipv4_key->ipv4_ttl, ipv4_mask->ipv4_ttl,
                          ovs_frag_type_to_string(ipv4_key->ipv4_frag),
                          ipv4_mask->ipv4_frag);
        } else {
            const struct ovs_key_ipv4 *ipv4_key = nl_attr_get(a);

            ds_put_format(ds, "src="IP_FMT",dst="IP_FMT",proto=%"PRIu8
                          ",tos=%#"PRIx8",ttl=%"PRIu8",frag=%s",
                          IP_ARGS(ipv4_key->ipv4_src),
                          IP_ARGS(ipv4_key->ipv4_dst),
                          ipv4_key->ipv4_proto, ipv4_key->ipv4_tos,
                          ipv4_key->ipv4_ttl,
                          ovs_frag_type_to_string(ipv4_key->ipv4_frag));
        }
        break;

    case OVS_KEY_ATTR_IPV6:
        if (!is_exact) {
            const struct ovs_key_ipv6 *ipv6_key, *ipv6_mask;
            char src_str[INET6_ADDRSTRLEN];
            char dst_str[INET6_ADDRSTRLEN];
            char src_mask[INET6_ADDRSTRLEN];
            char dst_mask[INET6_ADDRSTRLEN];

            ipv6_key = nl_attr_get(a);
            inet_ntop(AF_INET6, ipv6_key->ipv6_src, src_str, sizeof src_str);
            inet_ntop(AF_INET6, ipv6_key->ipv6_dst, dst_str, sizeof dst_str);

            ipv6_mask = nl_attr_get(ma);
            inet_ntop(AF_INET6, ipv6_mask->ipv6_src, src_mask, sizeof src_mask);
            inet_ntop(AF_INET6, ipv6_mask->ipv6_dst, dst_mask, sizeof dst_mask);

            ds_put_format(ds, "src=%s/%s,dst=%s/%s,label=%#"PRIx32"/%#"PRIx32
                          ",proto=%"PRIu8"/%#"PRIx8",tclass=%#"PRIx8"/%#"PRIx8
                          ",hlimit=%"PRIu8"/%#"PRIx8",frag=%s/%#"PRIx8,
                          src_str, src_mask, dst_str, dst_mask,
                          ntohl(ipv6_key->ipv6_label),
                          ntohl(ipv6_mask->ipv6_label),
                          ipv6_key->ipv6_proto, ipv6_mask->ipv6_proto,
                          ipv6_key->ipv6_tclass, ipv6_mask->ipv6_tclass,
                          ipv6_key->ipv6_hlimit, ipv6_mask->ipv6_hlimit,
                          ovs_frag_type_to_string(ipv6_key->ipv6_frag),
                          ipv6_mask->ipv6_frag);
        } else {
            const struct ovs_key_ipv6 *ipv6_key;
            char src_str[INET6_ADDRSTRLEN];
            char dst_str[INET6_ADDRSTRLEN];

            ipv6_key = nl_attr_get(a);
            inet_ntop(AF_INET6, ipv6_key->ipv6_src, src_str, sizeof src_str);
            inet_ntop(AF_INET6, ipv6_key->ipv6_dst, dst_str, sizeof dst_str);

            ds_put_format(ds, "src=%s,dst=%s,label=%#"PRIx32",proto=%"PRIu8
                          ",tclass=%#"PRIx8",hlimit=%"PRIu8",frag=%s",
                          src_str, dst_str, ntohl(ipv6_key->ipv6_label),
                          ipv6_key->ipv6_proto, ipv6_key->ipv6_tclass,
                          ipv6_key->ipv6_hlimit,
                          ovs_frag_type_to_string(ipv6_key->ipv6_frag));
        }
        break;

    case OVS_KEY_ATTR_TCP:
        if (!is_exact) {
            const struct ovs_key_tcp *tcp_mask = nl_attr_get(ma);
            const struct ovs_key_tcp *tcp_key = nl_attr_get(a);

            ds_put_format(ds, "src=%"PRIu16"/%#"PRIx16
                          ",dst=%"PRIu16"/%#"PRIx16,
                          ntohs(tcp_key->tcp_src), ntohs(tcp_mask->tcp_src),
                          ntohs(tcp_key->tcp_dst), ntohs(tcp_mask->tcp_dst));
        } else {
            const struct ovs_key_tcp *tcp_key = nl_attr_get(a);

            ds_put_format(ds, "src=%"PRIu16",dst=%"PRIu16,
                          ntohs(tcp_key->tcp_src), ntohs(tcp_key->tcp_dst));
        }
        break;

    case OVS_KEY_ATTR_UDP:
        if (!is_exact) {
            const struct ovs_key_udp *udp_mask = nl_attr_get(ma);
            const struct ovs_key_udp *udp_key = nl_attr_get(a);

            ds_put_format(ds, "src=%"PRIu16"/%#"PRIx16
                          ",dst=%"PRIu16"/%#"PRIx16,
                          ntohs(udp_key->udp_src), ntohs(udp_mask->udp_src),
                          ntohs(udp_key->udp_dst), ntohs(udp_mask->udp_dst));
        } else {
            const struct ovs_key_udp *udp_key = nl_attr_get(a);

            ds_put_format(ds, "src=%"PRIu16",dst=%"PRIu16,
                          ntohs(udp_key->udp_src), ntohs(udp_key->udp_dst));
        }
        break;

    case OVS_KEY_ATTR_SCTP:
        if (ma) {
            const struct ovs_key_sctp *sctp_mask = nl_attr_get(ma);
            const struct ovs_key_sctp *sctp_key = nl_attr_get(a);

            ds_put_format(ds, "src=%"PRIu16"/%#"PRIx16
                          ",dst=%"PRIu16"/%#"PRIx16,
                          ntohs(sctp_key->sctp_src), ntohs(sctp_mask->sctp_src),
                          ntohs(sctp_key->sctp_dst), ntohs(sctp_mask->sctp_dst));
        } else {
            const struct ovs_key_sctp *sctp_key = nl_attr_get(a);

            ds_put_format(ds, "(src=%"PRIu16",dst=%"PRIu16")",
                          ntohs(sctp_key->sctp_src), ntohs(sctp_key->sctp_dst));
        }
        break;

    case OVS_KEY_ATTR_ICMP:
        if (!is_exact) {
            const struct ovs_key_icmp *icmp_mask = nl_attr_get(ma);
            const struct ovs_key_icmp *icmp_key = nl_attr_get(a);

            ds_put_format(ds, "type=%"PRIu8"/%#"PRIx8",code=%"PRIu8"/%#"PRIx8,
                          icmp_key->icmp_type, icmp_mask->icmp_type,
                          icmp_key->icmp_code, icmp_mask->icmp_code);
        } else {
            const struct ovs_key_icmp *icmp_key = nl_attr_get(a);

            ds_put_format(ds, "type=%"PRIu8",code=%"PRIu8,
                          icmp_key->icmp_type, icmp_key->icmp_code);
        }
        break;

    case OVS_KEY_ATTR_ICMPV6:
        if (!is_exact) {
            const struct ovs_key_icmpv6 *icmpv6_mask = nl_attr_get(ma);
            const struct ovs_key_icmpv6 *icmpv6_key = nl_attr_get(a);

            ds_put_format(ds, "type=%"PRIu8"/%#"PRIx8",code=%"PRIu8"/%#"PRIx8,
                          icmpv6_key->icmpv6_type, icmpv6_mask->icmpv6_type,
                          icmpv6_key->icmpv6_code, icmpv6_mask->icmpv6_code);
        } else {
            const struct ovs_key_icmpv6 *icmpv6_key = nl_attr_get(a);

            ds_put_format(ds, "type=%"PRIu8",code=%"PRIu8,
                          icmpv6_key->icmpv6_type, icmpv6_key->icmpv6_code);
        }
        break;

    case OVS_KEY_ATTR_ARP:
        if (!is_exact) {
            const struct ovs_key_arp *arp_mask = nl_attr_get(ma);
            const struct ovs_key_arp *arp_key = nl_attr_get(a);

            ds_put_format(ds, "sip="IP_FMT"/"IP_FMT",tip="IP_FMT"/"IP_FMT
                          ",op=%"PRIu16"/%#"PRIx16
                          ",sha="ETH_ADDR_FMT"/"ETH_ADDR_FMT
                          ",tha="ETH_ADDR_FMT"/"ETH_ADDR_FMT,
                          IP_ARGS(arp_key->arp_sip),
                          IP_ARGS(arp_mask->arp_sip),
                          IP_ARGS(arp_key->arp_tip),
                          IP_ARGS(arp_mask->arp_tip),
                          ntohs(arp_key->arp_op), ntohs(arp_mask->arp_op),
                          ETH_ADDR_ARGS(arp_key->arp_sha),
                          ETH_ADDR_ARGS(arp_mask->arp_sha),
                          ETH_ADDR_ARGS(arp_key->arp_tha),
                          ETH_ADDR_ARGS(arp_mask->arp_tha));
        } else {
            const struct ovs_key_arp *arp_key = nl_attr_get(a);

            ds_put_format(ds, "sip="IP_FMT",tip="IP_FMT",op=%"PRIu16","
                          "sha="ETH_ADDR_FMT",tha="ETH_ADDR_FMT,
                          IP_ARGS(arp_key->arp_sip), IP_ARGS(arp_key->arp_tip),
                          ntohs(arp_key->arp_op),
                          ETH_ADDR_ARGS(arp_key->arp_sha),
                          ETH_ADDR_ARGS(arp_key->arp_tha));
        }
        break;

    case OVS_KEY_ATTR_ND: {
        const struct ovs_key_nd *nd_key, *nd_mask = NULL;
        char target[INET6_ADDRSTRLEN];

        nd_key = nl_attr_get(a);
        if (!is_exact) {
            nd_mask = nl_attr_get(ma);
        }

        inet_ntop(AF_INET6, nd_key->nd_target, target, sizeof target);
        ds_put_format(ds, "target=%s", target);
        if (!is_exact) {
            inet_ntop(AF_INET6, nd_mask->nd_target, target, sizeof target);
            ds_put_format(ds, "/%s", target);
        }

        if (!eth_addr_is_zero(nd_key->nd_sll)) {
            ds_put_format(ds, ",sll="ETH_ADDR_FMT,
                          ETH_ADDR_ARGS(nd_key->nd_sll));
            if (!is_exact) {
                ds_put_format(ds, "/"ETH_ADDR_FMT,
                              ETH_ADDR_ARGS(nd_mask->nd_sll));
            }
        }
        if (!eth_addr_is_zero(nd_key->nd_tll)) {
            ds_put_format(ds, ",tll="ETH_ADDR_FMT,
                          ETH_ADDR_ARGS(nd_key->nd_tll));
            if (!is_exact) {
                ds_put_format(ds, "/"ETH_ADDR_FMT,
                              ETH_ADDR_ARGS(nd_mask->nd_tll));
            }
        }
        break;
    }

    case OVS_KEY_ATTR_UNSPEC:
    case __OVS_KEY_ATTR_MAX:
    default:
        format_generic_odp_key(a, ds);
        if (!is_exact) {
            ds_put_char(ds, '/');
            format_generic_odp_key(ma, ds);
        }
        break;
    }
    ds_put_char(ds, ')');
}

static struct nlattr *
generate_all_wildcard_mask(struct ofpbuf *ofp, const struct nlattr *key)
{
    const struct nlattr *a;
    unsigned int left;
    int type = nl_attr_type(key);
    int size = nl_attr_get_size(key);

    if (odp_flow_key_attr_len(type) >=0) {
        memset(nl_msg_put_unspec_uninit(ofp, type, size), 0, size);
    } else {
        size_t nested_mask;

        nested_mask = nl_msg_start_nested(ofp, type);
        NL_ATTR_FOR_EACH(a, left, key, nl_attr_get_size(key)) {
            generate_all_wildcard_mask(ofp, nl_attr_get(a));
        }
        nl_msg_end_nested(ofp, nested_mask);
    }

    return ofp->base;
}

/* Appends to 'ds' a string representation of the 'key_len' bytes of
 * OVS_KEY_ATTR_* attributes in 'key'. If non-null, additionally formats the
 * 'mask_len' bytes of 'mask' which apply to 'key'. */
void
odp_flow_format(const struct nlattr *key, size_t key_len,
                const struct nlattr *mask, size_t mask_len,
                struct ds *ds, bool verbose)
{
    if (key_len) {
        const struct nlattr *a;
        unsigned int left;
        bool has_ethtype_key = false;
        const struct nlattr *ma = NULL;
        struct ofpbuf ofp;
        bool first_field = true;

        ofpbuf_init(&ofp, 100);
        NL_ATTR_FOR_EACH (a, left, key, key_len) {
            bool is_nested_attr;
            bool is_wildcard = false;
            int attr_type = nl_attr_type(a);

            if (attr_type == OVS_KEY_ATTR_ETHERTYPE) {
                has_ethtype_key = true;
            }

            is_nested_attr = (odp_flow_key_attr_len(attr_type) == -2);

            if (mask && mask_len) {
                ma = nl_attr_find__(mask, mask_len, nl_attr_type(a));
                is_wildcard = ma ? odp_mask_attr_is_wildcard(ma) : true;
            }

            if (verbose || !is_wildcard  || is_nested_attr) {
                if (is_wildcard && !ma) {
                    ma = generate_all_wildcard_mask(&ofp, a);
                }
                if (!first_field) {
                    ds_put_char(ds, ',');
                }
                format_odp_key_attr(a, ma, ds, verbose);
                first_field = false;
            }
            ofpbuf_clear(&ofp);
        }
        ofpbuf_uninit(&ofp);

        if (left) {
            int i;
            
            if (left == key_len) {
                ds_put_cstr(ds, "<empty>");
            }
            ds_put_format(ds, ",***%u leftover bytes*** (", left);
            for (i = 0; i < left; i++) {
                ds_put_format(ds, "%02x", ((const uint8_t *) a)[i]);
            }
            ds_put_char(ds, ')');
        }
        if (!has_ethtype_key) {
            ma = nl_attr_find__(mask, mask_len, OVS_KEY_ATTR_ETHERTYPE);
            if (ma) {
                ds_put_format(ds, ",eth_type(0/0x%04"PRIx16")",
                              ntohs(nl_attr_get_be16(ma)));
            }
        }
    } else {
        ds_put_cstr(ds, "<empty>");
    }
}

/* Appends to 'ds' a string representation of the 'key_len' bytes of
 * OVS_KEY_ATTR_* attributes in 'key'. */
void
odp_flow_key_format(const struct nlattr *key,
                    size_t key_len, struct ds *ds)
{
    odp_flow_format(key, key_len, NULL, 0, ds, true);
}

static void
put_nd(struct ovs_key_nd* nd_key, const uint8_t *nd_sll,
       const uint8_t *nd_tll, struct ofpbuf *key)
{
    if (nd_sll) {
        memcpy(nd_key->nd_sll, nd_sll, ETH_ADDR_LEN);
    }

    if (nd_tll) {
        memcpy(nd_key->nd_tll, nd_tll, ETH_ADDR_LEN);
    }

    nl_msg_put_unspec(key, OVS_KEY_ATTR_ND, nd_key, sizeof *nd_key);
}

static int
put_nd_key(int n, const char *nd_target_s, const uint8_t *nd_sll,
           const uint8_t *nd_tll, struct ofpbuf *key)
{
    struct ovs_key_nd nd_key;

    memset(&nd_key, 0, sizeof nd_key);

    if (inet_pton(AF_INET6, nd_target_s, nd_key.nd_target) != 1) {
        return -EINVAL;
    }

    put_nd(&nd_key, nd_sll, nd_tll, key);
    return n;
}

static int
put_nd_mask(int n, const char *nd_target_s,
           const uint8_t *nd_sll, const uint8_t *nd_tll, struct ofpbuf *mask)
{
    struct ovs_key_nd nd_mask;

    memset(&nd_mask, 0xff, sizeof nd_mask);

    if (strlen(nd_target_s) != 0 &&
            inet_pton(AF_INET6, nd_target_s, nd_mask.nd_target) != 1) {
        return -EINVAL;
    }

    put_nd(&nd_mask, nd_sll, nd_tll, mask);
    return n;
}

static bool
ovs_frag_type_from_string(const char *s, enum ovs_frag_type *type)
{
    if (!strcasecmp(s, "no")) {
        *type = OVS_FRAG_TYPE_NONE;
    } else if (!strcasecmp(s, "first")) {
        *type = OVS_FRAG_TYPE_FIRST;
    } else if (!strcasecmp(s, "later")) {
        *type = OVS_FRAG_TYPE_LATER;
    } else {
        return false;
    }
    return true;
}

static ovs_be32
mpls_lse_from_components(int mpls_label, int mpls_tc, int mpls_ttl, int mpls_bos)
{
    return (htonl((mpls_label << MPLS_LABEL_SHIFT) |
                  (mpls_tc << MPLS_TC_SHIFT)       |
                  (mpls_ttl << MPLS_TTL_SHIFT)     |
                  (mpls_bos << MPLS_BOS_SHIFT)));
}

static int
parse_odp_key_mask_attr(const char *s, const struct simap *port_names,
                        struct ofpbuf *key, struct ofpbuf *mask)
{
    /* Many of the sscanf calls in this function use oversized destination
     * fields because some sscanf() implementations truncate the range of %i
     * directives, so that e.g. "%"SCNi16 interprets input of "0xfedc" as a
     * value of 0x7fff.  The other alternatives are to allow only a single
     * radix (e.g. decimal or hexadecimal) or to write more sophisticated
     * parsers.
     *
     * The tun_id parser has to use an alternative approach because there is no
     * type larger than 64 bits. */

    {
        unsigned long long int priority;
        unsigned long long int priority_mask;
        int n = -1;

        if (mask && sscanf(s, "skb_priority(%lli/%lli)%n", &priority,
                   &priority_mask, &n) > 0 && n > 0) {
            nl_msg_put_u32(key, OVS_KEY_ATTR_PRIORITY, priority);
            nl_msg_put_u32(mask, OVS_KEY_ATTR_PRIORITY, priority_mask);
            return n;
        } else if (sscanf(s, "skb_priority(%lli)%n",
                          &priority, &n) > 0 && n > 0) {
            nl_msg_put_u32(key, OVS_KEY_ATTR_PRIORITY, priority);
            if (mask) {
                nl_msg_put_u32(mask, OVS_KEY_ATTR_PRIORITY, UINT32_MAX);
            }
            return n;
        }
    }

    {
        unsigned long long int mark;
        unsigned long long int mark_mask;
        int n = -1;

        if (mask && sscanf(s, "skb_mark(%lli/%lli)%n", &mark,
                   &mark_mask, &n) > 0 && n > 0) {
            nl_msg_put_u32(key, OVS_KEY_ATTR_SKB_MARK, mark);
            nl_msg_put_u32(mask, OVS_KEY_ATTR_SKB_MARK, mark_mask);
            return n;
        } else if (sscanf(s, "skb_mark(%lli)%n", &mark, &n) > 0 && n > 0) {
            nl_msg_put_u32(key, OVS_KEY_ATTR_SKB_MARK, mark);
            if (mask) {
                nl_msg_put_u32(mask, OVS_KEY_ATTR_SKB_MARK, UINT32_MAX);
            }
            return n;
        }
    }

    {
        char tun_id_s[32];
        int tos, tos_mask, ttl, ttl_mask;
        struct flow_tnl tun_key, tun_key_mask;
        unsigned long long tun_id_mask;
        int n = -1;

        if (mask && sscanf(s, "tunnel(tun_id=%31[x0123456789abcdefABCDEF]/%llx,"
                   "src="IP_SCAN_FMT"/"IP_SCAN_FMT",dst="IP_SCAN_FMT
                   "/"IP_SCAN_FMT",tos=%i/%i,ttl=%i/%i,flags%n",
                   tun_id_s, &tun_id_mask,
                   IP_SCAN_ARGS(&tun_key.ip_src),
                   IP_SCAN_ARGS(&tun_key_mask.ip_src),
                   IP_SCAN_ARGS(&tun_key.ip_dst),
                   IP_SCAN_ARGS(&tun_key_mask.ip_dst),
                   &tos, &tos_mask, &ttl, &ttl_mask,
                   &n) > 0 && n > 0) {
            int res;
            uint32_t flags;

            tun_key.tun_id = htonll(strtoull(tun_id_s, NULL, 0));
            tun_key_mask.tun_id = htonll(tun_id_mask);
            tun_key.ip_tos = tos;
            tun_key_mask.ip_tos = tos_mask;
            tun_key.ip_ttl = ttl;
            tun_key_mask.ip_ttl = ttl_mask;
            res = parse_flags(&s[n], flow_tun_flag_to_string, &flags);
            tun_key.flags = flags;
            tun_key_mask.flags = UINT16_MAX;

            if (res < 0) {
                return res;
            }
            n += res;
            if (s[n] != ')') {
                return -EINVAL;
            }
            n++;
            tun_key_to_attr(key, &tun_key);
            if (mask) {
                tun_key_to_attr(mask, &tun_key_mask);
            }
            return n;
        } else if (sscanf(s, "tunnel(tun_id=%31[x0123456789abcdefABCDEF],"
                   "src="IP_SCAN_FMT",dst="IP_SCAN_FMT
                   ",tos=%i,ttl=%i,flags%n", tun_id_s,
                    IP_SCAN_ARGS(&tun_key.ip_src),
                    IP_SCAN_ARGS(&tun_key.ip_dst), &tos, &ttl,
                    &n) > 0 && n > 0) {
            int res;
            uint32_t flags;

            tun_key.tun_id = htonll(strtoull(tun_id_s, NULL, 0));
            tun_key.ip_tos = tos;
            tun_key.ip_ttl = ttl;
            res = parse_flags(&s[n], flow_tun_flag_to_string, &flags);
            tun_key.flags = flags;

            if (res < 0) {
                return res;
            }
            n += res;
            if (s[n] != ')') {
                return -EINVAL;
            }
            n++;
            tun_key_to_attr(key, &tun_key);

            if (mask) {
                memset(&tun_key, 0xff, sizeof tun_key);
                tun_key_to_attr(mask, &tun_key);
            }
            return n;
        }
    }

    {
        unsigned long long int in_port;
        unsigned long long int in_port_mask;
        int n = -1;

        if (mask && sscanf(s, "in_port(%lli/%lli)%n", &in_port,
                   &in_port_mask, &n) > 0 && n > 0) {
            nl_msg_put_u32(key, OVS_KEY_ATTR_IN_PORT, in_port);
            nl_msg_put_u32(mask, OVS_KEY_ATTR_IN_PORT, in_port_mask);
            return n;
        } else if (sscanf(s, "in_port(%lli)%n", &in_port, &n) > 0 && n > 0) {
            nl_msg_put_u32(key, OVS_KEY_ATTR_IN_PORT, in_port);
            if (mask) {
                nl_msg_put_u32(mask, OVS_KEY_ATTR_IN_PORT, UINT32_MAX);
            }
            return n;
        }
    }


    if (port_names && !strncmp(s, "in_port(", 8)) {
        const char *name;
        const struct simap_node *node;
        int name_len;

        name = s + 8;
        name_len = strcspn(s, ")");
        node = simap_find_len(port_names, name, name_len);
        if (node) {
            nl_msg_put_u32(key, OVS_KEY_ATTR_IN_PORT, node->data);

            if (mask) {
                nl_msg_put_u32(mask, OVS_KEY_ATTR_IN_PORT, UINT32_MAX);
            }
            return 8 + name_len + 1;
        }
    }

    {
        struct ovs_key_ethernet eth_key;
        struct ovs_key_ethernet eth_key_mask;
        int n = -1;

        if (mask && sscanf(s,
                   "eth(src="ETH_ADDR_SCAN_FMT"/"ETH_ADDR_SCAN_FMT","
                        "dst="ETH_ADDR_SCAN_FMT"/"ETH_ADDR_SCAN_FMT")%n",
                ETH_ADDR_SCAN_ARGS(eth_key.eth_src),
                ETH_ADDR_SCAN_ARGS(eth_key_mask.eth_src),
                ETH_ADDR_SCAN_ARGS(eth_key.eth_dst),
                ETH_ADDR_SCAN_ARGS(eth_key_mask.eth_dst), &n) > 0 && n > 0) {

            nl_msg_put_unspec(key, OVS_KEY_ATTR_ETHERNET,
                              &eth_key, sizeof eth_key);
            nl_msg_put_unspec(mask, OVS_KEY_ATTR_ETHERNET,
                              &eth_key_mask, sizeof eth_key_mask);
            return n;
        } else if (sscanf(s,
                   "eth(src="ETH_ADDR_SCAN_FMT",dst="ETH_ADDR_SCAN_FMT")%n",
                   ETH_ADDR_SCAN_ARGS(eth_key.eth_src),
                   ETH_ADDR_SCAN_ARGS(eth_key.eth_dst), &n) > 0 && n > 0) {
            nl_msg_put_unspec(key, OVS_KEY_ATTR_ETHERNET,
                              &eth_key, sizeof eth_key);

            if (mask) {
                memset(&eth_key, 0xff, sizeof eth_key);
                nl_msg_put_unspec(mask, OVS_KEY_ATTR_ETHERNET,
                              &eth_key, sizeof eth_key);
            }
            return n;
        }
    }

    {
        uint16_t vid, vid_mask;
        int pcp, pcp_mask;
        int cfi, cfi_mask;
        int n = -1;

        if (mask && (sscanf(s, "vlan(vid=%"SCNi16"/%"SCNi16",pcp=%i/%i)%n",
                            &vid, &vid_mask, &pcp, &pcp_mask, &n) > 0 && n > 0)) {
            nl_msg_put_be16(key, OVS_KEY_ATTR_VLAN,
                            htons((vid << VLAN_VID_SHIFT) |
                                  (pcp << VLAN_PCP_SHIFT) |
                                  VLAN_CFI));
            nl_msg_put_be16(mask, OVS_KEY_ATTR_VLAN,
                            htons((vid_mask << VLAN_VID_SHIFT) |
                                  (pcp_mask << VLAN_PCP_SHIFT) |
                                  (1 << VLAN_CFI_SHIFT)));
            return n;
        } else if ((sscanf(s, "vlan(vid=%"SCNi16",pcp=%i)%n",
                           &vid, &pcp, &n) > 0 && n > 0)) {
            nl_msg_put_be16(key, OVS_KEY_ATTR_VLAN,
                            htons((vid << VLAN_VID_SHIFT) |
                                  (pcp << VLAN_PCP_SHIFT) |
                                  VLAN_CFI));
            if (mask) {
                nl_msg_put_be16(mask, OVS_KEY_ATTR_VLAN, htons(UINT16_MAX));
            }
            return n;
        } else if (mask && (sscanf(s, "vlan(vid=%"SCNi16"/%"SCNi16",pcp=%i/%i,cfi=%i/%i)%n",
                                   &vid, &vid_mask, &pcp, &pcp_mask, &cfi, &cfi_mask, &n) > 0 && n > 0)) {
            nl_msg_put_be16(key, OVS_KEY_ATTR_VLAN,
                            htons((vid << VLAN_VID_SHIFT) |
                                  (pcp << VLAN_PCP_SHIFT) |
                                  (cfi ? VLAN_CFI : 0)));
            nl_msg_put_be16(mask, OVS_KEY_ATTR_VLAN,
                            htons((vid_mask << VLAN_VID_SHIFT) |
                                  (pcp_mask << VLAN_PCP_SHIFT) |
                                  (cfi_mask << VLAN_CFI_SHIFT)));
            return n;
        } else if ((sscanf(s, "vlan(vid=%"SCNi16",pcp=%i,cfi=%i)%n",
                           &vid, &pcp, &cfi, &n) > 0 && n > 0)) {
            nl_msg_put_be16(key, OVS_KEY_ATTR_VLAN,
                            htons((vid << VLAN_VID_SHIFT) |
                                  (pcp << VLAN_PCP_SHIFT) |
                                  (cfi ? VLAN_CFI : 0)));
            if (mask) {
                nl_msg_put_be16(mask, OVS_KEY_ATTR_VLAN, htons(UINT16_MAX));
            }
            return n;
        }
    }

    {
        int eth_type;
        int eth_type_mask;
        int n = -1;

        if (mask && sscanf(s, "eth_type(%i/%i)%n",
                   &eth_type, &eth_type_mask, &n) > 0 && n > 0) {
            if (eth_type != 0) {
                nl_msg_put_be16(key, OVS_KEY_ATTR_ETHERTYPE, htons(eth_type));
            }
            nl_msg_put_be16(mask, OVS_KEY_ATTR_ETHERTYPE, htons(eth_type_mask));
            return n;
        } else if (sscanf(s, "eth_type(%i)%n", &eth_type, &n) > 0 && n > 0) {
            nl_msg_put_be16(key, OVS_KEY_ATTR_ETHERTYPE, htons(eth_type));
            if (mask) {
                nl_msg_put_be16(mask, OVS_KEY_ATTR_ETHERTYPE,
                                htons(UINT16_MAX));
            }
            return n;
        }
    }

    {
        int label, tc, ttl, bos;
        int label_mask, tc_mask, ttl_mask, bos_mask;
        int n = -1;

        if (mask && sscanf(s, "mpls(label=%"SCNi32"/%"SCNi32",tc=%i/%i,ttl=%i/%i,bos=%i/%i)%n",
                    &label, &label_mask, &tc, &tc_mask, &ttl, &ttl_mask, &bos, &bos_mask, &n) > 0 && n > 0) {
            struct ovs_key_mpls *mpls, *mpls_mask;

            mpls = nl_msg_put_unspec_uninit(key, OVS_KEY_ATTR_MPLS,
                                            sizeof *mpls);
            mpls->mpls_lse = mpls_lse_from_components(label, tc, ttl, bos);

            mpls_mask = nl_msg_put_unspec_uninit(mask, OVS_KEY_ATTR_MPLS,
                                            sizeof *mpls_mask);
            mpls_mask->mpls_lse = mpls_lse_from_components(
                                  label_mask, tc_mask, ttl_mask, bos_mask);
            return n;
        } else if (sscanf(s, "mpls(label=%"SCNi32",tc=%i,ttl=%i,bos=%i)%n",
                    &label, &tc, &ttl, &bos, &n) > 0 &&
                    n > 0) {
            struct ovs_key_mpls *mpls;

            mpls = nl_msg_put_unspec_uninit(key, OVS_KEY_ATTR_MPLS,
                                            sizeof *mpls);
            mpls->mpls_lse = mpls_lse_from_components(label, tc, ttl, bos);
            if (mask) {
                mpls = nl_msg_put_unspec_uninit(mask, OVS_KEY_ATTR_MPLS,
                                            sizeof *mpls);
                mpls->mpls_lse = htonl(UINT32_MAX);
            }
            return n;
        }
    }


    {
        ovs_be32 ipv4_src, ipv4_src_mask;
        ovs_be32 ipv4_dst, ipv4_dst_mask;
        int ipv4_proto, ipv4_proto_mask;
        int ipv4_tos, ipv4_tos_mask;
        int ipv4_ttl, ipv4_ttl_mask;
        char frag[8];
        int  ipv4_frag_mask;
        enum ovs_frag_type ipv4_frag;
        int n = -1;

        if (mask && sscanf(s, "ipv4(src="IP_SCAN_FMT"/"IP_SCAN_FMT","
                      "dst="IP_SCAN_FMT"/"IP_SCAN_FMT","
                      "proto=%i/%i,tos=%i/%i,ttl=%i/%i,"
                      "frag=%7[a-z]/%i)%n",
                      IP_SCAN_ARGS(&ipv4_src), IP_SCAN_ARGS(&ipv4_src_mask),
                      IP_SCAN_ARGS(&ipv4_dst), IP_SCAN_ARGS(&ipv4_dst_mask),
                      &ipv4_proto, &ipv4_proto_mask,
                      &ipv4_tos, &ipv4_tos_mask, &ipv4_ttl, &ipv4_ttl_mask,
                      frag, &ipv4_frag_mask, &n) > 0
            && n > 0
            && ovs_frag_type_from_string(frag, &ipv4_frag)) {
            struct ovs_key_ipv4 ipv4_key;
            struct ovs_key_ipv4 ipv4_mask;

            ipv4_key.ipv4_src = ipv4_src;
            ipv4_key.ipv4_dst = ipv4_dst;
            ipv4_key.ipv4_proto = ipv4_proto;
            ipv4_key.ipv4_tos = ipv4_tos;
            ipv4_key.ipv4_ttl = ipv4_ttl;
            ipv4_key.ipv4_frag = ipv4_frag;
            nl_msg_put_unspec(key, OVS_KEY_ATTR_IPV4,
                              &ipv4_key, sizeof ipv4_key);

            ipv4_mask.ipv4_src = ipv4_src_mask;
            ipv4_mask.ipv4_dst = ipv4_dst_mask;
            ipv4_mask.ipv4_proto = ipv4_proto_mask;
            ipv4_mask.ipv4_tos = ipv4_tos_mask;
            ipv4_mask.ipv4_ttl = ipv4_ttl_mask;
            ipv4_mask.ipv4_frag = ipv4_frag_mask;
            nl_msg_put_unspec(mask, OVS_KEY_ATTR_IPV4,
                              &ipv4_mask, sizeof ipv4_mask);
            return n;
        } else if (sscanf(s, "ipv4(src="IP_SCAN_FMT",dst="IP_SCAN_FMT","
                   "proto=%i,tos=%i,ttl=%i,frag=%7[a-z])%n",
                   IP_SCAN_ARGS(&ipv4_src), IP_SCAN_ARGS(&ipv4_dst),
                   &ipv4_proto, &ipv4_tos, &ipv4_ttl, frag, &n) > 0
            && n > 0
            && ovs_frag_type_from_string(frag, &ipv4_frag)) {
            struct ovs_key_ipv4 ipv4_key;

            ipv4_key.ipv4_src = ipv4_src;
            ipv4_key.ipv4_dst = ipv4_dst;
            ipv4_key.ipv4_proto = ipv4_proto;
            ipv4_key.ipv4_tos = ipv4_tos;
            ipv4_key.ipv4_ttl = ipv4_ttl;
            ipv4_key.ipv4_frag = ipv4_frag;
            nl_msg_put_unspec(key, OVS_KEY_ATTR_IPV4,
                              &ipv4_key, sizeof ipv4_key);

            if (mask) {
                memset(&ipv4_key, 0xff, sizeof ipv4_key);
                nl_msg_put_unspec(mask, OVS_KEY_ATTR_IPV4,
                              &ipv4_key, sizeof ipv4_key);
            }
            return n;
        }
    }

    {
        char ipv6_src_s[IPV6_SCAN_LEN + 1];
        char ipv6_src_mask_s[IPV6_SCAN_LEN + 1];
        char ipv6_dst_s[IPV6_SCAN_LEN + 1];
        char ipv6_dst_mask_s[IPV6_SCAN_LEN + 1];
        int ipv6_label, ipv6_label_mask;
        int ipv6_proto, ipv6_proto_mask;
        int ipv6_tclass, ipv6_tclass_mask;
        int ipv6_hlimit, ipv6_hlimit_mask;
        char frag[8];
        enum ovs_frag_type ipv6_frag;
        int ipv6_frag_mask;
        int n = -1;

        if (mask && sscanf(s, "ipv6(src="IPV6_SCAN_FMT"/"IPV6_SCAN_FMT",dst="
                   IPV6_SCAN_FMT"/"IPV6_SCAN_FMT","
                   "label=%i/%i,proto=%i/%i,tclass=%i/%i,"
                   "hlimit=%i/%i,frag=%7[a-z]/%i)%n",
                   ipv6_src_s, ipv6_src_mask_s, ipv6_dst_s, ipv6_dst_mask_s,
                   &ipv6_label, &ipv6_label_mask, &ipv6_proto,
                   &ipv6_proto_mask, &ipv6_tclass, &ipv6_tclass_mask,
                   &ipv6_hlimit, &ipv6_hlimit_mask, frag,
                   &ipv6_frag_mask, &n) > 0
            && n > 0
            && ovs_frag_type_from_string(frag, &ipv6_frag)) {
            struct ovs_key_ipv6 ipv6_key;
            struct ovs_key_ipv6 ipv6_mask;

            if (inet_pton(AF_INET6, ipv6_src_s, &ipv6_key.ipv6_src) != 1 ||
                inet_pton(AF_INET6, ipv6_dst_s, &ipv6_key.ipv6_dst) != 1 ||
                inet_pton(AF_INET6, ipv6_src_mask_s, &ipv6_mask.ipv6_src) != 1 ||
                inet_pton(AF_INET6, ipv6_dst_mask_s, &ipv6_mask.ipv6_dst) != 1) {
                return -EINVAL;
            }

            ipv6_key.ipv6_label = htonl(ipv6_label);
            ipv6_key.ipv6_proto = ipv6_proto;
            ipv6_key.ipv6_tclass = ipv6_tclass;
            ipv6_key.ipv6_hlimit = ipv6_hlimit;
            ipv6_key.ipv6_frag = ipv6_frag;
            nl_msg_put_unspec(key, OVS_KEY_ATTR_IPV6,
                              &ipv6_key, sizeof ipv6_key);

            ipv6_mask.ipv6_label = htonl(ipv6_label_mask);
            ipv6_mask.ipv6_proto = ipv6_proto_mask;
            ipv6_mask.ipv6_tclass = ipv6_tclass_mask;
            ipv6_mask.ipv6_hlimit = ipv6_hlimit_mask;
            ipv6_mask.ipv6_frag = ipv6_frag_mask;
            nl_msg_put_unspec(mask, OVS_KEY_ATTR_IPV6,
                              &ipv6_mask, sizeof ipv6_mask);
            return n;
        } else if (sscanf(s, "ipv6(src="IPV6_SCAN_FMT",dst="IPV6_SCAN_FMT","
                   "label=%i,proto=%i,tclass=%i,hlimit=%i,frag=%7[a-z])%n",
                   ipv6_src_s, ipv6_dst_s, &ipv6_label,
                   &ipv6_proto, &ipv6_tclass, &ipv6_hlimit, frag, &n) > 0
            && n > 0
            && ovs_frag_type_from_string(frag, &ipv6_frag)) {
            struct ovs_key_ipv6 ipv6_key;

            if (inet_pton(AF_INET6, ipv6_src_s, &ipv6_key.ipv6_src) != 1 ||
                inet_pton(AF_INET6, ipv6_dst_s, &ipv6_key.ipv6_dst) != 1) {
                return -EINVAL;
            }
            ipv6_key.ipv6_label = htonl(ipv6_label);
            ipv6_key.ipv6_proto = ipv6_proto;
            ipv6_key.ipv6_tclass = ipv6_tclass;
            ipv6_key.ipv6_hlimit = ipv6_hlimit;
            ipv6_key.ipv6_frag = ipv6_frag;
            nl_msg_put_unspec(key, OVS_KEY_ATTR_IPV6,
                              &ipv6_key, sizeof ipv6_key);

            if (mask) {
                memset(&ipv6_key, 0xff, sizeof ipv6_key);
                nl_msg_put_unspec(mask, OVS_KEY_ATTR_IPV6,
                              &ipv6_key, sizeof ipv6_key);
            }
            return n;
        }
    }

    {
        int tcp_src;
        int tcp_dst;
        int tcp_src_mask;
        int tcp_dst_mask;
        int n = -1;

        if (mask && sscanf(s, "tcp(src=%i/%i,dst=%i/%i)%n",
                   &tcp_src, &tcp_src_mask, &tcp_dst, &tcp_dst_mask, &n) > 0
            && n > 0) {
            struct ovs_key_tcp tcp_key;
            struct ovs_key_tcp tcp_mask;

            tcp_key.tcp_src = htons(tcp_src);
            tcp_key.tcp_dst = htons(tcp_dst);
            nl_msg_put_unspec(key, OVS_KEY_ATTR_TCP, &tcp_key, sizeof tcp_key);

            tcp_mask.tcp_src = htons(tcp_src_mask);
            tcp_mask.tcp_dst = htons(tcp_dst_mask);
            nl_msg_put_unspec(mask, OVS_KEY_ATTR_TCP,
                              &tcp_mask, sizeof tcp_mask);
            return n;
        } else if (sscanf(s, "tcp(src=%i,dst=%i)%n",&tcp_src, &tcp_dst, &n) > 0
            && n > 0) {
            struct ovs_key_tcp tcp_key;

            tcp_key.tcp_src = htons(tcp_src);
            tcp_key.tcp_dst = htons(tcp_dst);
            nl_msg_put_unspec(key, OVS_KEY_ATTR_TCP, &tcp_key, sizeof tcp_key);

            if (mask) {
                memset(&tcp_key, 0xff, sizeof tcp_key);
                nl_msg_put_unspec(mask, OVS_KEY_ATTR_TCP,
                              &tcp_key, sizeof tcp_key);
            }
            return n;
        }
    }

    {
        int udp_src;
        int udp_dst;
        int udp_src_mask;
        int udp_dst_mask;
        int n = -1;

        if (mask && sscanf(s, "udp(src=%i/%i,dst=%i/%i)%n",
                   &udp_src, &udp_src_mask,
                   &udp_dst, &udp_dst_mask, &n) > 0 && n > 0) {
            struct ovs_key_udp udp_key;
            struct ovs_key_udp udp_mask;

            udp_key.udp_src = htons(udp_src);
            udp_key.udp_dst = htons(udp_dst);
            nl_msg_put_unspec(key, OVS_KEY_ATTR_UDP, &udp_key, sizeof udp_key);

            udp_mask.udp_src = htons(udp_src_mask);
            udp_mask.udp_dst = htons(udp_dst_mask);
            nl_msg_put_unspec(mask, OVS_KEY_ATTR_UDP,
                              &udp_mask, sizeof udp_mask);
            return n;
        }
        if (sscanf(s, "udp(src=%i,dst=%i)%n", &udp_src, &udp_dst, &n) > 0
            && n > 0) {
            struct ovs_key_udp udp_key;

            udp_key.udp_src = htons(udp_src);
            udp_key.udp_dst = htons(udp_dst);
            nl_msg_put_unspec(key, OVS_KEY_ATTR_UDP, &udp_key, sizeof udp_key);

            if (mask) {
                memset(&udp_key, 0xff, sizeof udp_key);
                nl_msg_put_unspec(mask, OVS_KEY_ATTR_UDP, &udp_key, sizeof udp_key);
            }
            return n;
        }
    }

    {
        int sctp_src;
        int sctp_dst;
        int sctp_src_mask;
        int sctp_dst_mask;
        int n = -1;

        if (mask && sscanf(s, "sctp(src=%i/%i,dst=%i/%i)%n",
                   &sctp_src, &sctp_src_mask,
                   &sctp_dst, &sctp_dst_mask, &n) > 0 && n > 0) {
            struct ovs_key_sctp sctp_key;
            struct ovs_key_sctp sctp_mask;

            sctp_key.sctp_src = htons(sctp_src);
            sctp_key.sctp_dst = htons(sctp_dst);
            nl_msg_put_unspec(key, OVS_KEY_ATTR_SCTP, &sctp_key, sizeof sctp_key);

            sctp_mask.sctp_src = htons(sctp_src_mask);
            sctp_mask.sctp_dst = htons(sctp_dst_mask);
            nl_msg_put_unspec(mask, OVS_KEY_ATTR_SCTP,
                              &sctp_mask, sizeof sctp_mask);
            return n;
        }
        if (sscanf(s, "sctp(src=%i,dst=%i)%n", &sctp_src, &sctp_dst, &n) > 0
            && n > 0) {
            struct ovs_key_sctp sctp_key;

            sctp_key.sctp_src = htons(sctp_src);
            sctp_key.sctp_dst = htons(sctp_dst);
            nl_msg_put_unspec(key, OVS_KEY_ATTR_SCTP, &sctp_key, sizeof sctp_key);

            if (mask) {
                memset(&sctp_key, 0xff, sizeof sctp_key);
                nl_msg_put_unspec(mask, OVS_KEY_ATTR_SCTP, &sctp_key, sizeof sctp_key);
            }
            return n;
        }
    }

    {
        int icmp_type;
        int icmp_code;
        int icmp_type_mask;
        int icmp_code_mask;
        int n = -1;

        if (mask && sscanf(s, "icmp(type=%i/%i,code=%i/%i)%n",
                   &icmp_type, &icmp_type_mask,
                   &icmp_code, &icmp_code_mask, &n) > 0 && n > 0) {
            struct ovs_key_icmp icmp_key;
            struct ovs_key_icmp icmp_mask;

            icmp_key.icmp_type = icmp_type;
            icmp_key.icmp_code = icmp_code;
            nl_msg_put_unspec(key, OVS_KEY_ATTR_ICMP,
                              &icmp_key, sizeof icmp_key);

            icmp_mask.icmp_type = icmp_type_mask;
            icmp_mask.icmp_code = icmp_code_mask;
            nl_msg_put_unspec(mask, OVS_KEY_ATTR_ICMP,
                              &icmp_mask, sizeof icmp_mask);
            return n;
        } else if (sscanf(s, "icmp(type=%i,code=%i)%n",
                   &icmp_type, &icmp_code, &n) > 0
            && n > 0) {
            struct ovs_key_icmp icmp_key;

            icmp_key.icmp_type = icmp_type;
            icmp_key.icmp_code = icmp_code;
            nl_msg_put_unspec(key, OVS_KEY_ATTR_ICMP,
                              &icmp_key, sizeof icmp_key);
            if (mask) {
                memset(&icmp_key, 0xff, sizeof icmp_key);
                nl_msg_put_unspec(mask, OVS_KEY_ATTR_ICMP, &icmp_key,
                              sizeof icmp_key);
            }
            return n;
        }
    }

    {
        struct ovs_key_icmpv6 icmpv6_key;
        struct ovs_key_icmpv6 icmpv6_mask;
        int icmpv6_type_mask;
        int icmpv6_code_mask;
        int n = -1;

        if (mask && sscanf(s, "icmpv6(type=%"SCNi8"/%i,code=%"SCNi8"/%i)%n",
                   &icmpv6_key.icmpv6_type, &icmpv6_type_mask,
                   &icmpv6_key.icmpv6_code, &icmpv6_code_mask, &n) > 0
            && n > 0) {
            nl_msg_put_unspec(key, OVS_KEY_ATTR_ICMPV6,
                              &icmpv6_key, sizeof icmpv6_key);

            icmpv6_mask.icmpv6_type = icmpv6_type_mask;
            icmpv6_mask.icmpv6_code = icmpv6_code_mask;
            nl_msg_put_unspec(mask, OVS_KEY_ATTR_ICMPV6, &icmpv6_mask,
                              sizeof icmpv6_mask);
            return n;
        } else if (sscanf(s, "icmpv6(type=%"SCNi8",code=%"SCNi8")%n",
                   &icmpv6_key.icmpv6_type, &icmpv6_key.icmpv6_code,&n) > 0
            && n > 0) {
            nl_msg_put_unspec(key, OVS_KEY_ATTR_ICMPV6,
                              &icmpv6_key, sizeof icmpv6_key);

            if (mask) {
                memset(&icmpv6_key, 0xff, sizeof icmpv6_key);
                nl_msg_put_unspec(mask, OVS_KEY_ATTR_ICMPV6, &icmpv6_key,
                              sizeof icmpv6_key);
            }
            return n;
        }
    }

    {
        ovs_be32 arp_sip, arp_sip_mask;
        ovs_be32 arp_tip, arp_tip_mask;
        int arp_op, arp_op_mask;
        uint8_t arp_sha[ETH_ADDR_LEN];
        uint8_t arp_sha_mask[ETH_ADDR_LEN];
        uint8_t arp_tha[ETH_ADDR_LEN];
        uint8_t arp_tha_mask[ETH_ADDR_LEN];
        int n = -1;

        if (mask && sscanf(s, "arp(sip="IP_SCAN_FMT"/"IP_SCAN_FMT","
                   "tip="IP_SCAN_FMT"/"IP_SCAN_FMT","
                   "op=%i/%i,sha="ETH_ADDR_SCAN_FMT"/"ETH_ADDR_SCAN_FMT","
                   "tha="ETH_ADDR_SCAN_FMT"/"ETH_ADDR_SCAN_FMT")%n",
                   IP_SCAN_ARGS(&arp_sip), IP_SCAN_ARGS(&arp_sip_mask),
                   IP_SCAN_ARGS(&arp_tip), IP_SCAN_ARGS(&arp_tip_mask),
                   &arp_op, &arp_op_mask,
                   ETH_ADDR_SCAN_ARGS(arp_sha),
                   ETH_ADDR_SCAN_ARGS(arp_sha_mask),
                   ETH_ADDR_SCAN_ARGS(arp_tha),
                   ETH_ADDR_SCAN_ARGS(arp_tha_mask), &n) > 0 && n > 0) {
            struct ovs_key_arp arp_key;
            struct ovs_key_arp arp_mask;

            memset(&arp_key, 0, sizeof arp_key);
            arp_key.arp_sip = arp_sip;
            arp_key.arp_tip = arp_tip;
            arp_key.arp_op = htons(arp_op);
            memcpy(arp_key.arp_sha, arp_sha, ETH_ADDR_LEN);
            memcpy(arp_key.arp_tha, arp_tha, ETH_ADDR_LEN);
            nl_msg_put_unspec(key, OVS_KEY_ATTR_ARP, &arp_key, sizeof arp_key);

            arp_mask.arp_sip = arp_sip_mask;
            arp_mask.arp_tip = arp_tip_mask;
            arp_mask.arp_op = htons(arp_op_mask);
            memcpy(arp_mask.arp_sha, arp_sha_mask, ETH_ADDR_LEN);
            memcpy(arp_mask.arp_tha, arp_tha_mask, ETH_ADDR_LEN);
            nl_msg_put_unspec(mask, OVS_KEY_ATTR_ARP,
                              &arp_mask, sizeof arp_mask);
            return n;
        } else if (sscanf(s, "arp(sip="IP_SCAN_FMT",tip="IP_SCAN_FMT","
                   "op=%i,sha="ETH_ADDR_SCAN_FMT",tha="ETH_ADDR_SCAN_FMT")%n",
                   IP_SCAN_ARGS(&arp_sip),
                   IP_SCAN_ARGS(&arp_tip),
                   &arp_op,
                   ETH_ADDR_SCAN_ARGS(arp_sha),
                   ETH_ADDR_SCAN_ARGS(arp_tha), &n) > 0 && n > 0) {
            struct ovs_key_arp arp_key;

            memset(&arp_key, 0, sizeof arp_key);
            arp_key.arp_sip = arp_sip;
            arp_key.arp_tip = arp_tip;
            arp_key.arp_op = htons(arp_op);
            memcpy(arp_key.arp_sha, arp_sha, ETH_ADDR_LEN);
            memcpy(arp_key.arp_tha, arp_tha, ETH_ADDR_LEN);
            nl_msg_put_unspec(key, OVS_KEY_ATTR_ARP, &arp_key, sizeof arp_key);

            if (mask) {
                memset(&arp_key, 0xff, sizeof arp_key);
                nl_msg_put_unspec(mask, OVS_KEY_ATTR_ARP,
                                  &arp_key, sizeof arp_key);
            }
            return n;
        }
    }

    {
        char nd_target_s[IPV6_SCAN_LEN + 1];
        char nd_target_mask_s[IPV6_SCAN_LEN + 1];
        uint8_t nd_sll[ETH_ADDR_LEN];
        uint8_t nd_sll_mask[ETH_ADDR_LEN];
        uint8_t nd_tll[ETH_ADDR_LEN];
        uint8_t nd_tll_mask[ETH_ADDR_LEN];
        int n = -1;

        nd_target_mask_s[0] = 0;
        memset(nd_sll_mask, 0xff, sizeof nd_sll_mask);
        memset(nd_tll_mask, 0xff, sizeof nd_tll_mask);

        if (mask && sscanf(s, "nd(target="IPV6_SCAN_FMT"/"IPV6_SCAN_FMT")%n",
                   nd_target_s, nd_target_mask_s, &n) > 0 && n > 0) {
                put_nd_key(n, nd_target_s, NULL, NULL, key);
                put_nd_mask(n, nd_target_mask_s, NULL, NULL, mask);
        } else if (sscanf(s, "nd(target="IPV6_SCAN_FMT")%n",
                   nd_target_s, &n) > 0 && n > 0) {
                put_nd_key(n, nd_target_s, NULL, NULL, key);
                if (mask) {
                    put_nd_mask(n, nd_target_mask_s, NULL, NULL, mask);
                }
        } else if (mask && sscanf(s, "nd(target="IPV6_SCAN_FMT"/"IPV6_SCAN_FMT
                         ",sll="ETH_ADDR_SCAN_FMT"/"ETH_ADDR_SCAN_FMT")%n",
                   nd_target_s, nd_target_mask_s,
                   ETH_ADDR_SCAN_ARGS(nd_sll),
                   ETH_ADDR_SCAN_ARGS(nd_sll_mask), &n) > 0 && n > 0) {
            put_nd_key(n, nd_target_s, nd_sll, NULL, key);
            put_nd_mask(n, nd_target_mask_s, nd_sll_mask, NULL, mask);
        } else if (sscanf(s, "nd(target="IPV6_SCAN_FMT",sll="ETH_ADDR_SCAN_FMT")%n",
                   nd_target_s, ETH_ADDR_SCAN_ARGS(nd_sll), &n) > 0
            && n > 0) {
            put_nd_key(n, nd_target_s, nd_sll, NULL, key);
            if (mask) {
                put_nd_mask(n, nd_target_mask_s, nd_sll_mask, NULL, mask);
            }
        } else if (mask && sscanf(s, "nd(target="IPV6_SCAN_FMT"/"IPV6_SCAN_FMT
                         ",tll="ETH_ADDR_SCAN_FMT"/"ETH_ADDR_SCAN_FMT")%n",
                   nd_target_s, nd_target_mask_s,
                   ETH_ADDR_SCAN_ARGS(nd_tll),
                   ETH_ADDR_SCAN_ARGS(nd_tll_mask), &n) > 0 && n > 0) {
            put_nd_key(n, nd_target_s, NULL, nd_tll, key);
            put_nd_mask(n, nd_target_mask_s, NULL, nd_tll_mask, mask);
        } else if (sscanf(s, "nd(target="IPV6_SCAN_FMT",tll="ETH_ADDR_SCAN_FMT")%n",
                   nd_target_s, ETH_ADDR_SCAN_ARGS(nd_tll), &n) > 0
            && n > 0) {
            put_nd_key(n, nd_target_s, NULL, nd_tll, key);
            if (mask) {
                put_nd_mask(n, nd_target_mask_s, NULL, nd_tll_mask, mask);
            }
        } else if (mask && sscanf(s, "nd(target="IPV6_SCAN_FMT"/"IPV6_SCAN_FMT
                   ",sll="ETH_ADDR_SCAN_FMT"/"ETH_ADDR_SCAN_FMT","
                   "tll="ETH_ADDR_SCAN_FMT"/"ETH_ADDR_SCAN_FMT")%n",
                   nd_target_s, nd_target_mask_s,
                   ETH_ADDR_SCAN_ARGS(nd_sll), ETH_ADDR_SCAN_ARGS(nd_sll_mask),
                   ETH_ADDR_SCAN_ARGS(nd_tll), ETH_ADDR_SCAN_ARGS(nd_tll_mask),
                   &n) > 0
            && n > 0) {
            put_nd_key(n, nd_target_s, nd_sll, nd_tll, key);
            put_nd_mask(n, nd_target_mask_s, nd_sll_mask, nd_tll_mask, mask);
        } else if (sscanf(s, "nd(target="IPV6_SCAN_FMT",sll="ETH_ADDR_SCAN_FMT","
                   "tll="ETH_ADDR_SCAN_FMT")%n",
                   nd_target_s, ETH_ADDR_SCAN_ARGS(nd_sll),
                   ETH_ADDR_SCAN_ARGS(nd_tll), &n) > 0
            && n > 0) {
            put_nd_key(n, nd_target_s, nd_sll, nd_tll, key);
            if (mask) {
                put_nd_mask(n, nd_target_mask_s,
                            nd_sll_mask, nd_tll_mask, mask);
            }
        }

        if (n != -1)
            return n;

    }

    if (!strncmp(s, "encap(", 6)) {
        const char *start = s;
        size_t encap, encap_mask = 0;

        encap = nl_msg_start_nested(key, OVS_KEY_ATTR_ENCAP);
        if (mask) {
            encap_mask = nl_msg_start_nested(mask, OVS_KEY_ATTR_ENCAP);
        }

        s += 6;
        for (;;) {
            int retval;

            s += strspn(s, ", \t\r\n");
            if (!*s) {
                return -EINVAL;
            } else if (*s == ')') {
                break;
            }

            retval = parse_odp_key_mask_attr(s, port_names, key, mask);
            if (retval < 0) {
                return retval;
            }
            s += retval;
        }
        s++;

        nl_msg_end_nested(key, encap);
        if (mask) {
            nl_msg_end_nested(mask, encap_mask);
        }

        return s - start;
    }

    return -EINVAL;
}

/* Parses the string representation of a datapath flow key, in the
 * format output by odp_flow_key_format().  Returns 0 if successful,
 * otherwise a positive errno value.  On success, the flow key is
 * appended to 'key' as a series of Netlink attributes.  On failure, no
 * data is appended to 'key'.  Either way, 'key''s data might be
 * reallocated.
 *
 * If 'port_names' is nonnull, it points to an simap that maps from a port name
 * to a port number.  (Port names may be used instead of port numbers in
 * in_port.)
 *
 * On success, the attributes appended to 'key' are individually syntactically
 * valid, but they may not be valid as a sequence.  'key' might, for example,
 * have duplicated keys.  odp_flow_key_to_flow() will detect those errors. */
int
odp_flow_from_string(const char *s, const struct simap *port_names,
                     struct ofpbuf *key, struct ofpbuf *mask)
{
    const size_t old_size = key->size;
    for (;;) {
        int retval;

        s += strspn(s, delimiters);
        if (!*s) {
            return 0;
        }

        retval = parse_odp_key_mask_attr(s, port_names, key, mask);
        if (retval < 0) {
            key->size = old_size;
            return -retval;
        }
        s += retval;
    }

    return 0;
}

static uint8_t
ovs_to_odp_frag(uint8_t nw_frag)
{
    return (nw_frag == 0 ? OVS_FRAG_TYPE_NONE
          : nw_frag == FLOW_NW_FRAG_ANY ? OVS_FRAG_TYPE_FIRST
          : OVS_FRAG_TYPE_LATER);
}

static uint8_t
ovs_to_odp_frag_mask(uint8_t nw_frag_mask)
{
    uint8_t frag_mask = ~(OVS_FRAG_TYPE_FIRST | OVS_FRAG_TYPE_LATER);

    frag_mask |= (nw_frag_mask & FLOW_NW_FRAG_ANY) ? OVS_FRAG_TYPE_FIRST : 0;
    frag_mask |= (nw_frag_mask & FLOW_NW_FRAG_LATER) ? OVS_FRAG_TYPE_LATER : 0;

    return frag_mask;
}

static void
odp_flow_key_from_flow__(struct ofpbuf *buf, const struct flow *data,
                         const struct flow *flow, odp_port_t odp_in_port)
{
    bool is_mask;
    struct ovs_key_ethernet *eth_key;
    size_t encap;

    /* We assume that if 'data' and 'flow' are not the same, we should
     * treat 'data' as a mask. */
    is_mask = (data != flow);

    nl_msg_put_u32(buf, OVS_KEY_ATTR_PRIORITY, data->skb_priority);

    if (flow->tunnel.ip_dst || is_mask) {
        tun_key_to_attr(buf, &data->tunnel);
    }

    nl_msg_put_u32(buf, OVS_KEY_ATTR_SKB_MARK, data->pkt_mark);

    /* Add an ingress port attribute if this is a mask or 'odp_in_port'
     * is not the magical value "ODPP_NONE". */
    if (is_mask || odp_in_port != ODPP_NONE) {
        nl_msg_put_odp_port(buf, OVS_KEY_ATTR_IN_PORT, odp_in_port);
    }

    eth_key = nl_msg_put_unspec_uninit(buf, OVS_KEY_ATTR_ETHERNET,
                                       sizeof *eth_key);
    memcpy(eth_key->eth_src, data->dl_src, ETH_ADDR_LEN);
    memcpy(eth_key->eth_dst, data->dl_dst, ETH_ADDR_LEN);

    if (flow->vlan_tci != htons(0) || flow->dl_type == htons(ETH_TYPE_VLAN)) {
        if (is_mask) {
            nl_msg_put_be16(buf, OVS_KEY_ATTR_ETHERTYPE, htons(UINT16_MAX));
        } else {
            nl_msg_put_be16(buf, OVS_KEY_ATTR_ETHERTYPE, htons(ETH_TYPE_VLAN));
        }
        nl_msg_put_be16(buf, OVS_KEY_ATTR_VLAN, data->vlan_tci);
        encap = nl_msg_start_nested(buf, OVS_KEY_ATTR_ENCAP);
        if (flow->vlan_tci == htons(0)) {
            goto unencap;
        }
    } else {
        encap = 0;
    }

    if (ntohs(flow->dl_type) < ETH_TYPE_MIN) {
        /* For backwards compatibility with kernels that don't support
         * wildcarding, the following convention is used to encode the
         * OVS_KEY_ATTR_ETHERTYPE for key and mask:
         *
         *   key      mask    matches
         * -------- --------  -------
         *  >0x5ff   0xffff   Specified Ethernet II Ethertype.
         *  >0x5ff      0     Any Ethernet II or non-Ethernet II frame.
         *  <none>   0xffff   Any non-Ethernet II frame (except valid
         *                    802.3 SNAP packet with valid eth_type).
         */
        if (is_mask) {
            nl_msg_put_be16(buf, OVS_KEY_ATTR_ETHERTYPE, htons(UINT16_MAX));
        }
        goto unencap;
    }

    nl_msg_put_be16(buf, OVS_KEY_ATTR_ETHERTYPE, data->dl_type);

    if (flow->dl_type == htons(ETH_TYPE_IP)) {
        struct ovs_key_ipv4 *ipv4_key;

        ipv4_key = nl_msg_put_unspec_uninit(buf, OVS_KEY_ATTR_IPV4,
                                            sizeof *ipv4_key);
        ipv4_key->ipv4_src = data->nw_src;
        ipv4_key->ipv4_dst = data->nw_dst;
        ipv4_key->ipv4_proto = data->nw_proto;
        ipv4_key->ipv4_tos = data->nw_tos;
        ipv4_key->ipv4_ttl = data->nw_ttl;
        ipv4_key->ipv4_frag = is_mask ? ovs_to_odp_frag_mask(data->nw_frag)
                                      : ovs_to_odp_frag(data->nw_frag);
    } else if (flow->dl_type == htons(ETH_TYPE_IPV6)) {
        struct ovs_key_ipv6 *ipv6_key;

        ipv6_key = nl_msg_put_unspec_uninit(buf, OVS_KEY_ATTR_IPV6,
                                            sizeof *ipv6_key);
        memcpy(ipv6_key->ipv6_src, &data->ipv6_src, sizeof ipv6_key->ipv6_src);
        memcpy(ipv6_key->ipv6_dst, &data->ipv6_dst, sizeof ipv6_key->ipv6_dst);
        ipv6_key->ipv6_label = data->ipv6_label;
        ipv6_key->ipv6_proto = data->nw_proto;
        ipv6_key->ipv6_tclass = data->nw_tos;
        ipv6_key->ipv6_hlimit = data->nw_ttl;
        ipv6_key->ipv6_frag = is_mask ? ovs_to_odp_frag_mask(data->nw_frag)
                                      : ovs_to_odp_frag(data->nw_frag);
    } else if (flow->dl_type == htons(ETH_TYPE_ARP) ||
               flow->dl_type == htons(ETH_TYPE_RARP)) {
        struct ovs_key_arp *arp_key;

        arp_key = nl_msg_put_unspec_uninit(buf, OVS_KEY_ATTR_ARP,
                                           sizeof *arp_key);
        memset(arp_key, 0, sizeof *arp_key);
        arp_key->arp_sip = data->nw_src;
        arp_key->arp_tip = data->nw_dst;
        arp_key->arp_op = htons(data->nw_proto);
        memcpy(arp_key->arp_sha, data->arp_sha, ETH_ADDR_LEN);
        memcpy(arp_key->arp_tha, data->arp_tha, ETH_ADDR_LEN);
    }

    if (flow->mpls_depth) {
        struct ovs_key_mpls *mpls_key;

        mpls_key = nl_msg_put_unspec_uninit(buf, OVS_KEY_ATTR_MPLS,
                                            sizeof *mpls_key);
        mpls_key->mpls_lse = data->mpls_lse;
    }

    if (is_ip_any(flow) && !(flow->nw_frag & FLOW_NW_FRAG_LATER)) {
        if (flow->nw_proto == IPPROTO_TCP) {
            struct ovs_key_tcp *tcp_key;

            tcp_key = nl_msg_put_unspec_uninit(buf, OVS_KEY_ATTR_TCP,
                                               sizeof *tcp_key);
            tcp_key->tcp_src = data->tp_src;
            tcp_key->tcp_dst = data->tp_dst;
        } else if (flow->nw_proto == IPPROTO_UDP) {
            struct ovs_key_udp *udp_key;

            udp_key = nl_msg_put_unspec_uninit(buf, OVS_KEY_ATTR_UDP,
                                               sizeof *udp_key);
            udp_key->udp_src = data->tp_src;
            udp_key->udp_dst = data->tp_dst;
        } else if (flow->nw_proto == IPPROTO_SCTP) {
            struct ovs_key_sctp *sctp_key;

            sctp_key = nl_msg_put_unspec_uninit(buf, OVS_KEY_ATTR_SCTP,
                                               sizeof *sctp_key);
            sctp_key->sctp_src = data->tp_src;
            sctp_key->sctp_dst = data->tp_dst;
        } else if (flow->dl_type == htons(ETH_TYPE_IP)
                && flow->nw_proto == IPPROTO_ICMP) {
            struct ovs_key_icmp *icmp_key;

            icmp_key = nl_msg_put_unspec_uninit(buf, OVS_KEY_ATTR_ICMP,
                                                sizeof *icmp_key);
            icmp_key->icmp_type = ntohs(data->tp_src);
            icmp_key->icmp_code = ntohs(data->tp_dst);
        } else if (flow->dl_type == htons(ETH_TYPE_IPV6)
                && flow->nw_proto == IPPROTO_ICMPV6) {
            struct ovs_key_icmpv6 *icmpv6_key;

            icmpv6_key = nl_msg_put_unspec_uninit(buf, OVS_KEY_ATTR_ICMPV6,
                                                  sizeof *icmpv6_key);
            icmpv6_key->icmpv6_type = ntohs(data->tp_src);
            icmpv6_key->icmpv6_code = ntohs(data->tp_dst);

            if (flow->tp_dst == htons(0) &&
                (flow->tp_src == htons(ND_NEIGHBOR_SOLICIT) ||
                 flow->tp_src == htons(ND_NEIGHBOR_ADVERT)) &&
                (!is_mask || (data->tp_src == htons(0xffff) &&
                              data->tp_dst == htons(0xffff)))) {

                struct ovs_key_nd *nd_key;

                nd_key = nl_msg_put_unspec_uninit(buf, OVS_KEY_ATTR_ND,
                                                    sizeof *nd_key);
                memcpy(nd_key->nd_target, &data->nd_target,
                        sizeof nd_key->nd_target);
                memcpy(nd_key->nd_sll, data->arp_sha, ETH_ADDR_LEN);
                memcpy(nd_key->nd_tll, data->arp_tha, ETH_ADDR_LEN);
            }
        }
    }

unencap:
    if (encap) {
        nl_msg_end_nested(buf, encap);
    }
}

/* Appends a representation of 'flow' as OVS_KEY_ATTR_* attributes to 'buf'.
 * 'flow->in_port' is ignored (since it is likely to be an OpenFlow port
 * number rather than a datapath port number).  Instead, if 'odp_in_port'
 * is anything other than ODPP_NONE, it is included in 'buf' as the input
 * port.
 *
 * 'buf' must have at least ODPUTIL_FLOW_KEY_BYTES bytes of space, or be
 * capable of being expanded to allow for that much space. */
void
odp_flow_key_from_flow(struct ofpbuf *buf, const struct flow *flow,
                       odp_port_t odp_in_port)
{
    odp_flow_key_from_flow__(buf, flow, flow, odp_in_port);
}

/* Appends a representation of 'mask' as OVS_KEY_ATTR_* attributes to
 * 'buf'.  'flow' is used as a template to determine how to interpret
 * 'mask'.  For example, the 'dl_type' of 'mask' describes the mask, but
 * it doesn't indicate whether the other fields should be interpreted as
 * ARP, IPv4, IPv6, etc.
 *
 * 'buf' must have at least ODPUTIL_FLOW_KEY_BYTES bytes of space, or be
 * capable of being expanded to allow for that much space. */
void
odp_flow_key_from_mask(struct ofpbuf *buf, const struct flow *mask,
                       const struct flow *flow, uint32_t odp_in_port_mask)
{
    odp_flow_key_from_flow__(buf, mask, flow, u32_to_odp(odp_in_port_mask));
}

uint32_t
odp_flow_key_hash(const struct nlattr *key, size_t key_len)
{
    BUILD_ASSERT_DECL(!(NLA_ALIGNTO % sizeof(uint32_t)));
    return hash_words(ALIGNED_CAST(const uint32_t *, key),
                      key_len / sizeof(uint32_t), 0);
}

static void
log_odp_key_attributes(struct vlog_rate_limit *rl, const char *title,
                       uint64_t attrs, int out_of_range_attr,
                       const struct nlattr *key, size_t key_len)
{
    struct ds s;
    int i;

    if (VLOG_DROP_DBG(rl)) {
        return;
    }

    ds_init(&s);
    for (i = 0; i < 64; i++) {
        if (attrs & (UINT64_C(1) << i)) {
            char namebuf[OVS_KEY_ATTR_BUFSIZE];

            ds_put_format(&s, " %s",
                          ovs_key_attr_to_string(i, namebuf, sizeof namebuf));
        }
    }
    if (out_of_range_attr) {
        ds_put_format(&s, " %d (and possibly others)", out_of_range_attr);
    }

    ds_put_cstr(&s, ": ");
    odp_flow_key_format(key, key_len, &s);

    VLOG_DBG("%s:%s", title, ds_cstr(&s));
    ds_destroy(&s);
}

static bool
odp_to_ovs_frag(uint8_t odp_frag, struct flow *flow)
{
    static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);

    if (odp_frag > OVS_FRAG_TYPE_LATER) {
        VLOG_ERR_RL(&rl, "invalid frag %"PRIu8" in flow key", odp_frag);
        return false;
    }

    if (odp_frag != OVS_FRAG_TYPE_NONE) {
        flow->nw_frag |= FLOW_NW_FRAG_ANY;
        if (odp_frag == OVS_FRAG_TYPE_LATER) {
            flow->nw_frag |= FLOW_NW_FRAG_LATER;
        }
    }
    return true;
}

static bool
parse_flow_nlattrs(const struct nlattr *key, size_t key_len,
                   const struct nlattr *attrs[], uint64_t *present_attrsp,
                   int *out_of_range_attrp)
{
    static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(10, 10);
    const struct nlattr *nla;
    uint64_t present_attrs;
    size_t left;

    BUILD_ASSERT(OVS_KEY_ATTR_MAX < CHAR_BIT * sizeof present_attrs);
    present_attrs = 0;
    *out_of_range_attrp = 0;
    NL_ATTR_FOR_EACH (nla, left, key, key_len) {
        uint16_t type = nl_attr_type(nla);
        size_t len = nl_attr_get_size(nla);
        int expected_len = odp_flow_key_attr_len(type);

        if (len != expected_len && expected_len >= 0) {
            char namebuf[OVS_KEY_ATTR_BUFSIZE];

            VLOG_ERR_RL(&rl, "attribute %s has length %zu but should have "
                        "length %d", ovs_key_attr_to_string(type, namebuf,
                                                            sizeof namebuf),
                        len, expected_len);
            return false;
        }

        if (type > OVS_KEY_ATTR_MAX) {
            *out_of_range_attrp = type;
        } else {
            if (present_attrs & (UINT64_C(1) << type)) {
                char namebuf[OVS_KEY_ATTR_BUFSIZE];

                VLOG_ERR_RL(&rl, "duplicate %s attribute in flow key",
                            ovs_key_attr_to_string(type,
                                                   namebuf, sizeof namebuf));
                return false;
            }

            present_attrs |= UINT64_C(1) << type;
            attrs[type] = nla;
        }
    }
    if (left) {
        VLOG_ERR_RL(&rl, "trailing garbage in flow key");
        return false;
    }

    *present_attrsp = present_attrs;
    return true;
}

static enum odp_key_fitness
check_expectations(uint64_t present_attrs, int out_of_range_attr,
                   uint64_t expected_attrs,
                   const struct nlattr *key, size_t key_len)
{
    uint64_t missing_attrs;
    uint64_t extra_attrs;

    missing_attrs = expected_attrs & ~present_attrs;
    if (missing_attrs) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(10, 10);
        log_odp_key_attributes(&rl, "expected but not present",
                               missing_attrs, 0, key, key_len);
        return ODP_FIT_TOO_LITTLE;
    }

    extra_attrs = present_attrs & ~expected_attrs;
    if (extra_attrs || out_of_range_attr) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(10, 10);
        log_odp_key_attributes(&rl, "present but not expected",
                               extra_attrs, out_of_range_attr, key, key_len);
        return ODP_FIT_TOO_MUCH;
    }

    return ODP_FIT_PERFECT;
}

static bool
parse_ethertype(const struct nlattr *attrs[OVS_KEY_ATTR_MAX + 1],
                uint64_t present_attrs, uint64_t *expected_attrs,
                struct flow *flow, const struct flow *src_flow)
{
    static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
    bool is_mask = flow != src_flow;

    if (present_attrs & (UINT64_C(1) << OVS_KEY_ATTR_ETHERTYPE)) {
        flow->dl_type = nl_attr_get_be16(attrs[OVS_KEY_ATTR_ETHERTYPE]);
        if (!is_mask && ntohs(flow->dl_type) < ETH_TYPE_MIN) {
            VLOG_ERR_RL(&rl, "invalid Ethertype %"PRIu16" in flow key",
                        ntohs(flow->dl_type));
            return false;
        }
        if (is_mask && ntohs(src_flow->dl_type) < ETH_TYPE_MIN &&
            flow->dl_type != htons(0xffff)) {
            return false;
        }
        *expected_attrs |= UINT64_C(1) << OVS_KEY_ATTR_ETHERTYPE;
    } else {
        if (!is_mask) {
            flow->dl_type = htons(FLOW_DL_TYPE_NONE);
        } else if (ntohs(src_flow->dl_type) < ETH_TYPE_MIN) {
            /* See comments in odp_flow_key_from_flow__(). */
            VLOG_ERR_RL(&rl, "mask expected for non-Ethernet II frame");
            return false;
        }
    }
    return true;
}

static enum odp_key_fitness
parse_l2_5_onward(const struct nlattr *attrs[OVS_KEY_ATTR_MAX + 1],
                  uint64_t present_attrs, int out_of_range_attr,
                  uint64_t expected_attrs, struct flow *flow,
                  const struct nlattr *key, size_t key_len,
		  const struct flow *src_flow)
{
    static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
    bool is_mask = src_flow != flow;
    const void *check_start = NULL;
    size_t check_len = 0;
    enum ovs_key_attr expected_bit = 0xff;

    if (eth_type_mpls(src_flow->dl_type)) {
	if (!is_mask) {
	    expected_attrs |= (UINT64_C(1) << OVS_KEY_ATTR_MPLS);

	    if (!(present_attrs & (UINT64_C(1) << OVS_KEY_ATTR_MPLS))) {
	        return ODP_FIT_TOO_LITTLE;
	    }
	    flow->mpls_lse = nl_attr_get_be32(attrs[OVS_KEY_ATTR_MPLS]);
	    flow->mpls_depth++;
        } else if (present_attrs & (UINT64_C(1) << OVS_KEY_ATTR_MPLS)) {
            flow->mpls_lse = nl_attr_get_be32(attrs[OVS_KEY_ATTR_MPLS]);

            if (flow->mpls_lse != 0 && flow->dl_type != htons(0xffff)) {
                return ODP_FIT_ERROR;
            }
            expected_attrs |= (UINT64_C(1) << OVS_KEY_ATTR_MPLS);
            if (flow->mpls_lse) {
                /* XXX Is this needed? */
                flow->mpls_depth = 0xffff;
            }
        }
        goto done;
    } else if (src_flow->dl_type == htons(ETH_TYPE_IP)) {
        if (!is_mask) {
            expected_attrs |= UINT64_C(1) << OVS_KEY_ATTR_IPV4;
        }
        if (present_attrs & (UINT64_C(1) << OVS_KEY_ATTR_IPV4)) {
            const struct ovs_key_ipv4 *ipv4_key;

            ipv4_key = nl_attr_get(attrs[OVS_KEY_ATTR_IPV4]);
            flow->nw_src = ipv4_key->ipv4_src;
            flow->nw_dst = ipv4_key->ipv4_dst;
            flow->nw_proto = ipv4_key->ipv4_proto;
            flow->nw_tos = ipv4_key->ipv4_tos;
            flow->nw_ttl = ipv4_key->ipv4_ttl;
            if (is_mask) {
	        flow->nw_frag = ipv4_key->ipv4_frag;
                check_start = ipv4_key;
                check_len = sizeof *ipv4_key;
                expected_bit = OVS_KEY_ATTR_IPV4;
            } else if (!odp_to_ovs_frag(ipv4_key->ipv4_frag, flow)) {
                return ODP_FIT_ERROR;
            }
        }
    } else if (src_flow->dl_type == htons(ETH_TYPE_IPV6)) {
        if (!is_mask) {
            expected_attrs |= UINT64_C(1) << OVS_KEY_ATTR_IPV6;
        }
        if (present_attrs & (UINT64_C(1) << OVS_KEY_ATTR_IPV6)) {
            const struct ovs_key_ipv6 *ipv6_key;

            ipv6_key = nl_attr_get(attrs[OVS_KEY_ATTR_IPV6]);
            memcpy(&flow->ipv6_src, ipv6_key->ipv6_src, sizeof flow->ipv6_src);
            memcpy(&flow->ipv6_dst, ipv6_key->ipv6_dst, sizeof flow->ipv6_dst);
            flow->ipv6_label = ipv6_key->ipv6_label;
            flow->nw_proto = ipv6_key->ipv6_proto;
            flow->nw_tos = ipv6_key->ipv6_tclass;
            flow->nw_ttl = ipv6_key->ipv6_hlimit;
            if (is_mask) {
	        flow->nw_frag = ipv6_key->ipv6_frag;
                check_start = ipv6_key;
                check_len = sizeof *ipv6_key;
                expected_bit = OVS_KEY_ATTR_IPV6;
            } else if (!odp_to_ovs_frag(ipv6_key->ipv6_frag, flow)) {
                return ODP_FIT_ERROR;
            }
        }
    } else if (src_flow->dl_type == htons(ETH_TYPE_ARP) ||
               src_flow->dl_type == htons(ETH_TYPE_RARP)) {
        if (!is_mask) {
            expected_attrs |= UINT64_C(1) << OVS_KEY_ATTR_ARP;
        }
        if (present_attrs & (UINT64_C(1) << OVS_KEY_ATTR_ARP)) {
            const struct ovs_key_arp *arp_key;

            arp_key = nl_attr_get(attrs[OVS_KEY_ATTR_ARP]);
            flow->nw_src = arp_key->arp_sip;
            flow->nw_dst = arp_key->arp_tip;
            if (!is_mask && (arp_key->arp_op & htons(0xff00))) {
                VLOG_ERR_RL(&rl, "unsupported ARP opcode %"PRIu16" in flow "
                            "key", ntohs(arp_key->arp_op));
                return ODP_FIT_ERROR;
            }
            flow->nw_proto = ntohs(arp_key->arp_op);
            memcpy(flow->arp_sha, arp_key->arp_sha, ETH_ADDR_LEN);
            memcpy(flow->arp_tha, arp_key->arp_tha, ETH_ADDR_LEN);

            if (is_mask) {
                check_start = arp_key;
                check_len = sizeof *arp_key;
                expected_bit = OVS_KEY_ATTR_ARP;
            }
        }
    } else {
        goto done;
    }
    if (is_mask) {
        if (!is_all_zeros(check_start, check_len) &&
            flow->dl_type != htons(0xffff)) {
            return ODP_FIT_ERROR;
        } else {
            expected_attrs |= UINT64_C(1) << expected_bit;
        }
    }

    expected_bit = OVS_KEY_ATTR_UNSPEC;
    if (src_flow->nw_proto == IPPROTO_TCP
        && (src_flow->dl_type == htons(ETH_TYPE_IP) ||
            src_flow->dl_type == htons(ETH_TYPE_IPV6))
        && !(src_flow->nw_frag & FLOW_NW_FRAG_LATER)) {
        if (!is_mask) {
            expected_attrs |= UINT64_C(1) << OVS_KEY_ATTR_TCP;
        }
        if (present_attrs & (UINT64_C(1) << OVS_KEY_ATTR_TCP)) {
            const struct ovs_key_tcp *tcp_key;

            tcp_key = nl_attr_get(attrs[OVS_KEY_ATTR_TCP]);
            flow->tp_src = tcp_key->tcp_src;
            flow->tp_dst = tcp_key->tcp_dst;
            expected_bit = OVS_KEY_ATTR_TCP;
        }
    } else if (src_flow->nw_proto == IPPROTO_UDP
               && (src_flow->dl_type == htons(ETH_TYPE_IP) ||
                   src_flow->dl_type == htons(ETH_TYPE_IPV6))
               && !(src_flow->nw_frag & FLOW_NW_FRAG_LATER)) {
        if (!is_mask) {
            expected_attrs |= UINT64_C(1) << OVS_KEY_ATTR_UDP;
        }
        if (present_attrs & (UINT64_C(1) << OVS_KEY_ATTR_UDP)) {
            const struct ovs_key_udp *udp_key;

            udp_key = nl_attr_get(attrs[OVS_KEY_ATTR_UDP]);
            flow->tp_src = udp_key->udp_src;
            flow->tp_dst = udp_key->udp_dst;
            expected_bit = OVS_KEY_ATTR_UDP;
        }
    } else if (flow->nw_proto == IPPROTO_SCTP
               && (flow->dl_type == htons(ETH_TYPE_IP) ||
                   flow->dl_type == htons(ETH_TYPE_IPV6))
               && !(flow->nw_frag & FLOW_NW_FRAG_LATER)) {
        if (!is_mask) {
            expected_attrs |= UINT64_C(1) << OVS_KEY_ATTR_SCTP;
        }
        if (present_attrs & (UINT64_C(1) << OVS_KEY_ATTR_SCTP)) {
            const struct ovs_key_sctp *sctp_key;

            sctp_key = nl_attr_get(attrs[OVS_KEY_ATTR_SCTP]);
            flow->tp_src = sctp_key->sctp_src;
            flow->tp_dst = sctp_key->sctp_dst;
            expected_bit = OVS_KEY_ATTR_SCTP;
        }
    } else if (src_flow->nw_proto == IPPROTO_ICMP
               && src_flow->dl_type == htons(ETH_TYPE_IP)
               && !(src_flow->nw_frag & FLOW_NW_FRAG_LATER)) {
        if (!is_mask) {
            expected_attrs |= UINT64_C(1) << OVS_KEY_ATTR_ICMP;
        }
        if (present_attrs & (UINT64_C(1) << OVS_KEY_ATTR_ICMP)) {
            const struct ovs_key_icmp *icmp_key;

            icmp_key = nl_attr_get(attrs[OVS_KEY_ATTR_ICMP]);
            flow->tp_src = htons(icmp_key->icmp_type);
            flow->tp_dst = htons(icmp_key->icmp_code);
            expected_bit = OVS_KEY_ATTR_ICMP;
        }
    } else if (src_flow->nw_proto == IPPROTO_ICMPV6
               && src_flow->dl_type == htons(ETH_TYPE_IPV6)
               && !(src_flow->nw_frag & FLOW_NW_FRAG_LATER)) {
        if (!is_mask) {
            expected_attrs |= UINT64_C(1) << OVS_KEY_ATTR_ICMPV6;
        }
        if (present_attrs & (UINT64_C(1) << OVS_KEY_ATTR_ICMPV6)) {
            const struct ovs_key_icmpv6 *icmpv6_key;

            icmpv6_key = nl_attr_get(attrs[OVS_KEY_ATTR_ICMPV6]);
            flow->tp_src = htons(icmpv6_key->icmpv6_type);
            flow->tp_dst = htons(icmpv6_key->icmpv6_code);
            expected_bit = OVS_KEY_ATTR_ICMPV6;
            if (src_flow->tp_dst == htons(0) &&
                (src_flow->tp_src == htons(ND_NEIGHBOR_SOLICIT) ||
                 src_flow->tp_src == htons(ND_NEIGHBOR_ADVERT))) {
                if (!is_mask) {
                    expected_attrs |= UINT64_C(1) << OVS_KEY_ATTR_ND;
                }
                if (present_attrs & (UINT64_C(1) << OVS_KEY_ATTR_ND)) {
                    const struct ovs_key_nd *nd_key;

                    nd_key = nl_attr_get(attrs[OVS_KEY_ATTR_ND]);
                    memcpy(&flow->nd_target, nd_key->nd_target,
                           sizeof flow->nd_target);
                    memcpy(flow->arp_sha, nd_key->nd_sll, ETH_ADDR_LEN);
                    memcpy(flow->arp_tha, nd_key->nd_tll, ETH_ADDR_LEN);
                    if (is_mask) {
                        if (!is_all_zeros((const uint8_t *) nd_key,
                                          sizeof *nd_key) &&
                            (flow->tp_src != htons(0xffff) ||
                             flow->tp_dst != htons(0xffff))) {
                            return ODP_FIT_ERROR;
                        } else {
                            expected_attrs |= UINT64_C(1) << OVS_KEY_ATTR_ND;
                        }
                    }
                }
            }
        }
    }
    if (is_mask && expected_bit != OVS_KEY_ATTR_UNSPEC) {
        if ((flow->tp_src || flow->tp_dst) && flow->nw_proto != 0xff) {
            return ODP_FIT_ERROR;
        } else {
            expected_attrs |= UINT64_C(1) << expected_bit;
        }
    }

done:
    return check_expectations(present_attrs, out_of_range_attr, expected_attrs,
                              key, key_len);
}

/* Parse 802.1Q header then encapsulated L3 attributes. */
static enum odp_key_fitness
parse_8021q_onward(const struct nlattr *attrs[OVS_KEY_ATTR_MAX + 1],
                   uint64_t present_attrs, int out_of_range_attr,
                   uint64_t expected_attrs, struct flow *flow,
                   const struct nlattr *key, size_t key_len,
                   const struct flow *src_flow)
{
    static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
    bool is_mask = src_flow != flow;

    const struct nlattr *encap
        = (present_attrs & (UINT64_C(1) << OVS_KEY_ATTR_ENCAP)
           ? attrs[OVS_KEY_ATTR_ENCAP] : NULL);
    enum odp_key_fitness encap_fitness;
    enum odp_key_fitness fitness;
    ovs_be16 tci;

    /* Calculate fitness of outer attributes. */
    if (!is_mask) {
        expected_attrs |= ((UINT64_C(1) << OVS_KEY_ATTR_VLAN) |
                          (UINT64_C(1) << OVS_KEY_ATTR_ENCAP));
    } else {
        if (present_attrs & (UINT64_C(1) << OVS_KEY_ATTR_VLAN)) {
            expected_attrs |= (UINT64_C(1) << OVS_KEY_ATTR_VLAN);
        }
        if (present_attrs & (UINT64_C(1) << OVS_KEY_ATTR_ENCAP)) {
            expected_attrs |= (UINT64_C(1) << OVS_KEY_ATTR_ENCAP);
        }
    }
    fitness = check_expectations(present_attrs, out_of_range_attr,
                                 expected_attrs, key, key_len);

    /* Get the VLAN TCI value. */
    if (!is_mask && !(present_attrs & (UINT64_C(1) << OVS_KEY_ATTR_VLAN))) {
        return ODP_FIT_TOO_LITTLE;
    } else {
        tci = nl_attr_get_be16(attrs[OVS_KEY_ATTR_VLAN]);
        if (!is_mask) {
	    if (tci == htons(0)) {
                /* Corner case for a truncated 802.1Q header. */
	        if (fitness == ODP_FIT_PERFECT && nl_attr_get_size(encap)) {
		    return ODP_FIT_TOO_MUCH;
	        }
	        return fitness;
	    } else if (!(tci & htons(VLAN_CFI))) {
	        VLOG_ERR_RL(&rl, "OVS_KEY_ATTR_VLAN 0x%04"PRIx16" is nonzero "
	                    "but CFI bit is not set", ntohs(tci));
	        return ODP_FIT_ERROR;
	    }
        }
        /* Set vlan_tci.
         * Remove the TPID from dl_type since it's not the real Ethertype.  */
        flow->dl_type = htons(0);
        flow->vlan_tci = tci;
    }

    if (is_mask && !(present_attrs & (UINT64_C(1) << OVS_KEY_ATTR_ENCAP))) {
        return fitness;
    }
    /* Now parse the encapsulated attributes. */
    if (!parse_flow_nlattrs(nl_attr_get(encap), nl_attr_get_size(encap),
                            attrs, &present_attrs, &out_of_range_attr)) {
        return ODP_FIT_ERROR;
    }
    expected_attrs = 0;

    if (!parse_ethertype(attrs, present_attrs, &expected_attrs, flow, src_flow)) {
        return ODP_FIT_ERROR;
    }
    encap_fitness = parse_l2_5_onward(attrs, present_attrs, out_of_range_attr,
                                      expected_attrs, flow, key, key_len,
                                      src_flow);

    /* The overall fitness is the worse of the outer and inner attributes. */
    return MAX(fitness, encap_fitness);
}

static enum odp_key_fitness
odp_flow_key_to_flow__(const struct nlattr *key, size_t key_len,
                       struct flow *flow, const struct flow *src_flow)
{
    const struct nlattr *attrs[OVS_KEY_ATTR_MAX + 1];
    uint64_t expected_attrs;
    uint64_t present_attrs;
    int out_of_range_attr;
    bool is_mask = src_flow != flow;

    memset(flow, 0, sizeof *flow);

    /* Parse attributes. */
    if (!parse_flow_nlattrs(key, key_len, attrs, &present_attrs,
                            &out_of_range_attr)) {
        return ODP_FIT_ERROR;
    }
    expected_attrs = 0;

    /* Metadata. */
    if (present_attrs & (UINT64_C(1) << OVS_KEY_ATTR_PRIORITY)) {
        flow->skb_priority = nl_attr_get_u32(attrs[OVS_KEY_ATTR_PRIORITY]);
        expected_attrs |= UINT64_C(1) << OVS_KEY_ATTR_PRIORITY;
    }

    if (present_attrs & (UINT64_C(1) << OVS_KEY_ATTR_SKB_MARK)) {
        flow->pkt_mark = nl_attr_get_u32(attrs[OVS_KEY_ATTR_SKB_MARK]);
        expected_attrs |= UINT64_C(1) << OVS_KEY_ATTR_SKB_MARK;
    }

    if (present_attrs & (UINT64_C(1) << OVS_KEY_ATTR_TUNNEL)) {
        enum odp_key_fitness res;

        res = odp_tun_key_from_attr(attrs[OVS_KEY_ATTR_TUNNEL], &flow->tunnel);
        if (res == ODP_FIT_ERROR) {
            return ODP_FIT_ERROR;
        } else if (res == ODP_FIT_PERFECT) {
            expected_attrs |= UINT64_C(1) << OVS_KEY_ATTR_TUNNEL;
        }
    }

    if (present_attrs & (UINT64_C(1) << OVS_KEY_ATTR_IN_PORT)) {
        flow->in_port.odp_port
            = nl_attr_get_odp_port(attrs[OVS_KEY_ATTR_IN_PORT]);
        expected_attrs |= UINT64_C(1) << OVS_KEY_ATTR_IN_PORT;
    } else if (!is_mask) {
        flow->in_port.odp_port = ODPP_NONE;
    }

    /* Ethernet header. */
    if (present_attrs & (UINT64_C(1) << OVS_KEY_ATTR_ETHERNET)) {
        const struct ovs_key_ethernet *eth_key;

        eth_key = nl_attr_get(attrs[OVS_KEY_ATTR_ETHERNET]);
        memcpy(flow->dl_src, eth_key->eth_src, ETH_ADDR_LEN);
        memcpy(flow->dl_dst, eth_key->eth_dst, ETH_ADDR_LEN);
        if (is_mask) {
            expected_attrs |= UINT64_C(1) << OVS_KEY_ATTR_ETHERNET;
        }
    }
    if (!is_mask) {
        expected_attrs |= UINT64_C(1) << OVS_KEY_ATTR_ETHERNET;
    }

    /* Get Ethertype or 802.1Q TPID or FLOW_DL_TYPE_NONE. */
    if (!parse_ethertype(attrs, present_attrs, &expected_attrs, flow,
        src_flow)) {
        return ODP_FIT_ERROR;
    }

    if ((is_mask && (src_flow->vlan_tci & htons(VLAN_CFI))) ||
        (!is_mask && src_flow->dl_type == htons(ETH_TYPE_VLAN))) {
        return parse_8021q_onward(attrs, present_attrs, out_of_range_attr,
                                  expected_attrs, flow, key, key_len, src_flow);
    }
    if (is_mask) {
        flow->vlan_tci = htons(0xffff);
        if (present_attrs & (UINT64_C(1) << OVS_KEY_ATTR_VLAN)) {
            flow->vlan_tci = nl_attr_get_be16(attrs[OVS_KEY_ATTR_VLAN]);
            expected_attrs |= (UINT64_C(1) << OVS_KEY_ATTR_VLAN);
        }
    }
    return parse_l2_5_onward(attrs, present_attrs, out_of_range_attr,
                             expected_attrs, flow, key, key_len, src_flow);
}

/* Converts the 'key_len' bytes of OVS_KEY_ATTR_* attributes in 'key' to a flow
 * structure in 'flow'.  Returns an ODP_FIT_* value that indicates how well
 * 'key' fits our expectations for what a flow key should contain.
 *
 * The 'in_port' will be the datapath's understanding of the port.  The
 * caller will need to translate with odp_port_to_ofp_port() if the
 * OpenFlow port is needed.
 *
 * This function doesn't take the packet itself as an argument because none of
 * the currently understood OVS_KEY_ATTR_* attributes require it.  Currently,
 * it is always possible to infer which additional attribute(s) should appear
 * by looking at the attributes for lower-level protocols, e.g. if the network
 * protocol in OVS_KEY_ATTR_IPV4 or OVS_KEY_ATTR_IPV6 is IPPROTO_TCP then we
 * know that a OVS_KEY_ATTR_TCP attribute must appear and that otherwise it
 * must be absent. */
enum odp_key_fitness
odp_flow_key_to_flow(const struct nlattr *key, size_t key_len,
                     struct flow *flow)
{
   return odp_flow_key_to_flow__(key, key_len, flow, flow);
}

/* Converts the 'key_len' bytes of OVS_KEY_ATTR_* attributes in 'key' to a mask
 * structure in 'mask'.  'flow' must be a previously translated flow
 * corresponding to 'mask'.  Returns an ODP_FIT_* value that indicates how well
 * 'key' fits our expectations for what a flow key should contain. */
enum odp_key_fitness
odp_flow_key_to_mask(const struct nlattr *key, size_t key_len,
                     struct flow *mask, const struct flow *flow)
{
   return odp_flow_key_to_flow__(key, key_len, mask, flow);
}

/* Returns 'fitness' as a string, for use in debug messages. */
const char *
odp_key_fitness_to_string(enum odp_key_fitness fitness)
{
    switch (fitness) {
    case ODP_FIT_PERFECT:
        return "OK";
    case ODP_FIT_TOO_MUCH:
        return "too_much";
    case ODP_FIT_TOO_LITTLE:
        return "too_little";
    case ODP_FIT_ERROR:
        return "error";
    default:
        return "<unknown>";
    }
}

/* Appends an OVS_ACTION_ATTR_USERSPACE action to 'odp_actions' that specifies
 * Netlink PID 'pid'.  If 'userdata' is nonnull, adds a userdata attribute
 * whose contents are the 'userdata_size' bytes at 'userdata' and returns the
 * offset within 'odp_actions' of the start of the cookie.  (If 'userdata' is
 * null, then the return value is not meaningful.) */
size_t
odp_put_userspace_action(uint32_t pid,
                         const void *userdata, size_t userdata_size,
                         struct ofpbuf *odp_actions)
{
    size_t userdata_ofs;
    size_t offset;

    offset = nl_msg_start_nested(odp_actions, OVS_ACTION_ATTR_USERSPACE);
    nl_msg_put_u32(odp_actions, OVS_USERSPACE_ATTR_PID, pid);
    if (userdata) {
        userdata_ofs = odp_actions->size + NLA_HDRLEN;
        nl_msg_put_unspec(odp_actions, OVS_USERSPACE_ATTR_USERDATA,
                          userdata, userdata_size);
    } else {
        userdata_ofs = 0;
    }
    nl_msg_end_nested(odp_actions, offset);

    return userdata_ofs;
}

void
odp_put_tunnel_action(const struct flow_tnl *tunnel,
                      struct ofpbuf *odp_actions)
{
    size_t offset = nl_msg_start_nested(odp_actions, OVS_ACTION_ATTR_SET);
    tun_key_to_attr(odp_actions, tunnel);
    nl_msg_end_nested(odp_actions, offset);
}

/* The commit_odp_actions() function and its helpers. */

static void
commit_set_action(struct ofpbuf *odp_actions, enum ovs_key_attr key_type,
                  const void *key, size_t key_size)
{
    size_t offset = nl_msg_start_nested(odp_actions, OVS_ACTION_ATTR_SET);
    nl_msg_put_unspec(odp_actions, key_type, key, key_size);
    nl_msg_end_nested(odp_actions, offset);
}

void
odp_put_pkt_mark_action(const uint32_t pkt_mark,
                        struct ofpbuf *odp_actions)
{
    commit_set_action(odp_actions, OVS_KEY_ATTR_SKB_MARK, &pkt_mark,
                      sizeof(pkt_mark));
}

/* If any of the flow key data that ODP actions can modify are different in
 * 'base->tunnel' and 'flow->tunnel', appends a set_tunnel ODP action to
 * 'odp_actions' that change the flow tunneling information in key from
 * 'base->tunnel' into 'flow->tunnel', and then changes 'base->tunnel' in the
 * same way.  In other words, operates the same as commit_odp_actions(), but
 * only on tunneling information. */
void
commit_odp_tunnel_action(const struct flow *flow, struct flow *base,
                         struct ofpbuf *odp_actions)
{
    /* A valid IPV4_TUNNEL must have non-zero ip_dst. */
    if (flow->tunnel.ip_dst) {
        if (!memcmp(&base->tunnel, &flow->tunnel, sizeof base->tunnel)) {
            return;
        }
        memcpy(&base->tunnel, &flow->tunnel, sizeof base->tunnel);
        odp_put_tunnel_action(&base->tunnel, odp_actions);
    }
}

static void
commit_set_ether_addr_action(const struct flow *flow, struct flow *base,
                             struct ofpbuf *odp_actions,
                             struct flow_wildcards *wc)
{
    struct ovs_key_ethernet eth_key;

    if (eth_addr_equals(base->dl_src, flow->dl_src) &&
        eth_addr_equals(base->dl_dst, flow->dl_dst)) {
        return;
    }

    memset(&wc->masks.dl_src, 0xff, sizeof wc->masks.dl_src);
    memset(&wc->masks.dl_dst, 0xff, sizeof wc->masks.dl_dst);

    memcpy(base->dl_src, flow->dl_src, ETH_ADDR_LEN);
    memcpy(base->dl_dst, flow->dl_dst, ETH_ADDR_LEN);

    memcpy(eth_key.eth_src, base->dl_src, ETH_ADDR_LEN);
    memcpy(eth_key.eth_dst, base->dl_dst, ETH_ADDR_LEN);

    commit_set_action(odp_actions, OVS_KEY_ATTR_ETHERNET,
                      &eth_key, sizeof(eth_key));
}

static void
commit_vlan_action(const struct flow *flow, struct flow *base,
                   struct ofpbuf *odp_actions, struct flow_wildcards *wc)
{
    if (base->vlan_tci == flow->vlan_tci) {
        return;
    }

    memset(&wc->masks.vlan_tci, 0xff, sizeof wc->masks.vlan_tci);

    if (base->vlan_tci & htons(VLAN_CFI)) {
        nl_msg_put_flag(odp_actions, OVS_ACTION_ATTR_POP_VLAN);
    }

    if (flow->vlan_tci & htons(VLAN_CFI)) {
        struct ovs_action_push_vlan vlan;

        vlan.vlan_tpid = htons(ETH_TYPE_VLAN);
        vlan.vlan_tci = flow->vlan_tci;
        nl_msg_put_unspec(odp_actions, OVS_ACTION_ATTR_PUSH_VLAN,
                          &vlan, sizeof vlan);
    }
    base->vlan_tci = flow->vlan_tci;
}

static void
commit_mpls_action(const struct flow *flow, struct flow *base,
                   struct ofpbuf *odp_actions, struct flow_wildcards *wc)
{
    if (flow->mpls_lse == base->mpls_lse &&
        flow->mpls_depth == base->mpls_depth) {
        return;
    }

    memset(&wc->masks.mpls_lse, 0xff, sizeof wc->masks.mpls_lse);

    if (flow->mpls_depth < base->mpls_depth) {
        if (base->mpls_depth - flow->mpls_depth > 1) {
            static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(10, 10);
            VLOG_WARN_RL(&rl, "Multiple mpls_pop actions reduced to "
                         " a single mpls_pop action");
        }

        nl_msg_put_be16(odp_actions, OVS_ACTION_ATTR_POP_MPLS, flow->dl_type);
    } else if (flow->mpls_depth > base->mpls_depth) {
        struct ovs_action_push_mpls *mpls;

        if (flow->mpls_depth - base->mpls_depth > 1) {
            static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(10, 10);
            VLOG_WARN_RL(&rl, "Multiple mpls_push actions reduced to "
                         " a single mpls_push action");
        }

        mpls = nl_msg_put_unspec_uninit(odp_actions, OVS_ACTION_ATTR_PUSH_MPLS,
                                        sizeof *mpls);
        memset(mpls, 0, sizeof *mpls);
        mpls->mpls_ethertype = flow->dl_type;
        mpls->mpls_lse = flow->mpls_lse;
    } else {
        struct ovs_key_mpls mpls_key;

        mpls_key.mpls_lse = flow->mpls_lse;
        commit_set_action(odp_actions, OVS_KEY_ATTR_MPLS,
                          &mpls_key, sizeof(mpls_key));
    }

    base->dl_type = flow->dl_type;
    base->mpls_lse = flow->mpls_lse;
    base->mpls_depth = flow->mpls_depth;
}

static void
commit_set_ipv4_action(const struct flow *flow, struct flow *base,
                     struct ofpbuf *odp_actions, struct flow_wildcards *wc)
{
    struct ovs_key_ipv4 ipv4_key;

    if (base->nw_src == flow->nw_src &&
        base->nw_dst == flow->nw_dst &&
        base->nw_tos == flow->nw_tos &&
        base->nw_ttl == flow->nw_ttl &&
        base->nw_frag == flow->nw_frag) {
        return;
    }

    memset(&wc->masks.nw_src, 0xff, sizeof wc->masks.nw_src);
    memset(&wc->masks.nw_dst, 0xff, sizeof wc->masks.nw_dst);
    memset(&wc->masks.nw_tos, 0xff, sizeof wc->masks.nw_tos);
    memset(&wc->masks.nw_ttl, 0xff, sizeof wc->masks.nw_ttl);
    memset(&wc->masks.nw_proto, 0xff, sizeof wc->masks.nw_proto);
    memset(&wc->masks.nw_frag, 0xff, sizeof wc->masks.nw_frag);

    ipv4_key.ipv4_src = base->nw_src = flow->nw_src;
    ipv4_key.ipv4_dst = base->nw_dst = flow->nw_dst;
    ipv4_key.ipv4_tos = base->nw_tos = flow->nw_tos;
    ipv4_key.ipv4_ttl = base->nw_ttl = flow->nw_ttl;
    ipv4_key.ipv4_proto = base->nw_proto;
    ipv4_key.ipv4_frag = ovs_to_odp_frag(base->nw_frag);

    commit_set_action(odp_actions, OVS_KEY_ATTR_IPV4,
                      &ipv4_key, sizeof(ipv4_key));
}

static void
commit_set_ipv6_action(const struct flow *flow, struct flow *base,
                       struct ofpbuf *odp_actions, struct flow_wildcards *wc)
{
    struct ovs_key_ipv6 ipv6_key;

    if (ipv6_addr_equals(&base->ipv6_src, &flow->ipv6_src) &&
        ipv6_addr_equals(&base->ipv6_dst, &flow->ipv6_dst) &&
        base->ipv6_label == flow->ipv6_label &&
        base->nw_tos == flow->nw_tos &&
        base->nw_ttl == flow->nw_ttl &&
        base->nw_frag == flow->nw_frag) {
        return;
    }

    memset(&wc->masks.ipv6_src, 0xff, sizeof wc->masks.ipv6_src);
    memset(&wc->masks.ipv6_dst, 0xff, sizeof wc->masks.ipv6_dst);
    memset(&wc->masks.ipv6_label, 0xff, sizeof wc->masks.ipv6_label);
    memset(&wc->masks.nw_tos, 0xff, sizeof wc->masks.nw_tos);
    memset(&wc->masks.nw_ttl, 0xff, sizeof wc->masks.nw_ttl);
    memset(&wc->masks.nw_proto, 0xff, sizeof wc->masks.nw_proto);
    memset(&wc->masks.nw_frag, 0xff, sizeof wc->masks.nw_frag);

    base->ipv6_src = flow->ipv6_src;
    memcpy(&ipv6_key.ipv6_src, &base->ipv6_src, sizeof(ipv6_key.ipv6_src));
    base->ipv6_dst = flow->ipv6_dst;
    memcpy(&ipv6_key.ipv6_dst, &base->ipv6_dst, sizeof(ipv6_key.ipv6_dst));

    ipv6_key.ipv6_label = base->ipv6_label = flow->ipv6_label;
    ipv6_key.ipv6_tclass = base->nw_tos = flow->nw_tos;
    ipv6_key.ipv6_hlimit = base->nw_ttl = flow->nw_ttl;
    ipv6_key.ipv6_proto = base->nw_proto;
    ipv6_key.ipv6_frag = ovs_to_odp_frag(base->nw_frag);

    commit_set_action(odp_actions, OVS_KEY_ATTR_IPV6,
                      &ipv6_key, sizeof(ipv6_key));
}

static void
commit_set_nw_action(const struct flow *flow, struct flow *base,
                     struct ofpbuf *odp_actions, struct flow_wildcards *wc)
{
    /* Check if flow really have an IP header. */
    if (!flow->nw_proto) {
        return;
    }

    if (base->dl_type == htons(ETH_TYPE_IP)) {
        commit_set_ipv4_action(flow, base, odp_actions, wc);
    } else if (base->dl_type == htons(ETH_TYPE_IPV6)) {
        commit_set_ipv6_action(flow, base, odp_actions, wc);
    }
}

static void
commit_set_port_action(const struct flow *flow, struct flow *base,
                       struct ofpbuf *odp_actions, struct flow_wildcards *wc)
{
    if (!is_ip_any(base) || (!base->tp_src && !base->tp_dst)) {
        return;
    }

    if (base->tp_src == flow->tp_src &&
        base->tp_dst == flow->tp_dst) {
        return;
    }

    memset(&wc->masks.tp_src, 0xff, sizeof wc->masks.tp_src);
    memset(&wc->masks.tp_dst, 0xff, sizeof wc->masks.tp_dst);

    if (flow->nw_proto == IPPROTO_TCP) {
        struct ovs_key_tcp port_key;

        port_key.tcp_src = base->tp_src = flow->tp_src;
        port_key.tcp_dst = base->tp_dst = flow->tp_dst;

        commit_set_action(odp_actions, OVS_KEY_ATTR_TCP,
                          &port_key, sizeof(port_key));

    } else if (flow->nw_proto == IPPROTO_UDP) {
        struct ovs_key_udp port_key;

        port_key.udp_src = base->tp_src = flow->tp_src;
        port_key.udp_dst = base->tp_dst = flow->tp_dst;

        commit_set_action(odp_actions, OVS_KEY_ATTR_UDP,
                          &port_key, sizeof(port_key));
    } else if (flow->nw_proto == IPPROTO_SCTP) {
        struct ovs_key_sctp port_key;

        port_key.sctp_src = base->tp_src = flow->tp_src;
        port_key.sctp_dst = base->tp_dst = flow->tp_dst;

        commit_set_action(odp_actions, OVS_KEY_ATTR_SCTP,
                          &port_key, sizeof(port_key));
    }
}

static void
commit_set_priority_action(const struct flow *flow, struct flow *base,
                           struct ofpbuf *odp_actions,
                           struct flow_wildcards *wc)
{
    if (base->skb_priority == flow->skb_priority) {
        return;
    }

    memset(&wc->masks.skb_priority, 0xff, sizeof wc->masks.skb_priority);
    base->skb_priority = flow->skb_priority;

    commit_set_action(odp_actions, OVS_KEY_ATTR_PRIORITY,
                      &base->skb_priority, sizeof(base->skb_priority));
}

static void
commit_set_pkt_mark_action(const struct flow *flow, struct flow *base,
                           struct ofpbuf *odp_actions,
                           struct flow_wildcards *wc)
{
    if (base->pkt_mark == flow->pkt_mark) {
        return;
    }

    memset(&wc->masks.pkt_mark, 0xff, sizeof wc->masks.pkt_mark);
    base->pkt_mark = flow->pkt_mark;

    odp_put_pkt_mark_action(base->pkt_mark, odp_actions);
}
/* If any of the flow key data that ODP actions can modify are different in
 * 'base' and 'flow', appends ODP actions to 'odp_actions' that change the flow
 * key from 'base' into 'flow', and then changes 'base' the same way.  Does not
 * commit set_tunnel actions.  Users should call commit_odp_tunnel_action()
 * in addition to this function if needed.  Sets fields in 'wc' that are
 * used as part of the action. */
void
commit_odp_actions(const struct flow *flow, struct flow *base,
                   struct ofpbuf *odp_actions, struct flow_wildcards *wc)
{
    commit_set_ether_addr_action(flow, base, odp_actions, wc);
    commit_vlan_action(flow, base, odp_actions, wc);
    commit_set_nw_action(flow, base, odp_actions, wc);
    commit_set_port_action(flow, base, odp_actions, wc);
    /* Committing MPLS actions should occur after committing nw and port
     * actions. This is because committing MPLS actions may alter a packet so
     * that it is no longer IP and thus nw and port actions are no longer valid.
     */
    commit_mpls_action(flow, base, odp_actions, wc);
    commit_set_priority_action(flow, base, odp_actions, wc);
    commit_set_pkt_mark_action(flow, base, odp_actions, wc);
}
