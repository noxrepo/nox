/* Copyright (c) 2012 Nicira, Inc.
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
 * limitations under the License.  */

#ifndef BFD_H
#define BFD_H 1

#define BFD_PACKET_LEN 24
#define BFD_DEST_PORT 3784

#include <stdbool.h>
#include <inttypes.h>

struct bfd;
struct flow;
struct flow_wildcards;
struct netdev;
struct ofpbuf;
struct smap;

void bfd_wait(const struct bfd *);
void bfd_run(struct bfd *);

bool bfd_should_send_packet(const struct bfd *);
void bfd_put_packet(struct bfd *bfd, struct ofpbuf *packet,
                    uint8_t eth_src[6]);

bool bfd_should_process_flow(const struct bfd *, const struct flow *,
                             struct flow_wildcards *);
void bfd_process_packet(struct bfd *, const struct flow *,
                        const struct ofpbuf *);

struct bfd *bfd_configure(struct bfd *, const char *name,
                          const struct smap *smap,
                          struct netdev *netdev);
struct bfd *bfd_ref(const struct bfd *);
void bfd_unref(struct bfd *);

bool bfd_forwarding(const struct bfd *);
void bfd_get_status(const struct bfd *, struct smap *);
void bfd_set_netdev(struct bfd *, const struct netdev *);

#endif /* bfd.h */
