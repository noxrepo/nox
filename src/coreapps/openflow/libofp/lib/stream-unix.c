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

#include <config.h>
#include "stream.h"
#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "packets.h"
#include "poll-loop.h"
#include "socket-util.h"
#include "dirs.h"
#include "util.h"
#include "stream-provider.h"
#include "stream-fd.h"
#include "vlog.h"

VLOG_DEFINE_THIS_MODULE(stream_unix);

/* Active UNIX socket. */

static int
unix_open(const char *name, char *suffix, struct stream **streamp,
          uint8_t dscp OVS_UNUSED)
{
    char *connect_path;
    int fd;

    connect_path = abs_file_name(ovs_rundir(), suffix);
    fd = make_unix_socket(SOCK_STREAM, true, NULL, connect_path);

    if (fd < 0) {
        VLOG_DBG("%s: connection failed (%s)",
                 connect_path, ovs_strerror(-fd));
        free(connect_path);
        return -fd;
    }

    free(connect_path);
    return new_fd_stream(name, fd, check_connection_completion(fd), streamp);
}

const struct stream_class unix_stream_class = {
    "unix",                     /* name */
    false,                      /* needs_probes */
    unix_open,                  /* open */
    NULL,                       /* close */
    NULL,                       /* connect */
    NULL,                       /* recv */
    NULL,                       /* send */
    NULL,                       /* run */
    NULL,                       /* run_wait */
    NULL,                       /* wait */
};

/* Passive UNIX socket. */

static int punix_accept(int fd, const struct sockaddr *sa, size_t sa_len,
                        struct stream **streamp);

static int
punix_open(const char *name OVS_UNUSED, char *suffix,
           struct pstream **pstreamp, uint8_t dscp OVS_UNUSED)
{
    char *bind_path;
    int fd, error;

    bind_path = abs_file_name(ovs_rundir(), suffix);
    fd = make_unix_socket(SOCK_STREAM, true, bind_path, NULL);
    if (fd < 0) {
        VLOG_ERR("%s: binding failed: %s", bind_path, ovs_strerror(errno));
        free(bind_path);
        return errno;
    }

    if (listen(fd, 10) < 0) {
        error = errno;
        VLOG_ERR("%s: listen: %s", name, ovs_strerror(error));
        close(fd);
        free(bind_path);
        return error;
    }

    return new_fd_pstream(name, fd, punix_accept, NULL, bind_path, pstreamp);
}

static int
punix_accept(int fd, const struct sockaddr *sa, size_t sa_len,
             struct stream **streamp)
{
    const struct sockaddr_un *sun = (const struct sockaddr_un *) sa;
    int name_len = get_unix_name_len(sa_len);
    char name[128];

    if (name_len > 0) {
        snprintf(name, sizeof name, "unix:%.*s", name_len, sun->sun_path);
    } else {
        strcpy(name, "unix");
    }
    return new_fd_stream(name, fd, 0, streamp);
}

const struct pstream_class punix_pstream_class = {
    "punix",
    false,
    punix_open,
    NULL,
    NULL,
    NULL,
    NULL,
};

