/* Copyright 2008, 2009 (C) Nicira, Inc.
 *
 * This file is part of NOX.
 *
 * NOX is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * NOX is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with NOX.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "vlog-socket.hh"
#include <boost/bind.hpp>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "shutdown-event.hh"
#include "string.hh"
#include "vlog.hh"

#ifndef SCM_CREDENTIALS
#include <sys/stat.h>
#include <time.h>
#endif

namespace vigil
{

static int make_unix_socket(bool nonblock, bool passcred,
                            const std::string* bind_path,
                            const std::string* connect_path);

static void vlog_server_fsm(Vlog&, int fd);

/* Server socket. */
Vlog_server_socket::Vlog_server_socket(Vlog& vlog_)
    : vlog(vlog_), fd(-1)
{}

Vlog_server_socket::~Vlog_server_socket()
{
    close();
}

/* Start listening for connections from clients and processing their
 * requests.  'path' may be:
 *
 *      - NULL, in which case the default socket path is used.  (Only one
 *        Vlog_server_socket per process can use the default path.)
 *
 *      - A name that does not start with '/', in which case it is appended to
 *        the default socket path.
 *
 *      - An absolute path (starting with '/') that gives the exact name of
 *        the Unix domain socket to listen on.
 *
 * Returns 0 if successful, otherwise a positive errno value. */
int
Vlog_server_socket::listen(const char* path_)
{
    close();

    /* Form absolute path. */
    if (path_ && path_[0] == '/')
    {
        path = path_;
    }
    else
    {
        path = string_format("/tmp/vlogs.%ld", (long int) getpid());
        if (path_)
        {
            path += path_;
        }
    }

    /* Create socket. */
    fd = make_unix_socket(true, true, &path, NULL);
    if (fd < 0)
    {
        fprintf(stderr, "Could not initialize vlog configuration socket: %s\n",
                strerror(-fd));
        return -fd;
    }

    /* Start listening. */
    vlog_server_fsm(boost::ref(vlog), fd);

    return 0;
}

/* Stops listening for connections. */
void
Vlog_server_socket::close()
{
    if (fd >= 0)
    {
        ::close(fd);
        fd = -1;
        unlink(path.c_str());
    }
}

static int
recv_with_creds(int fd,
                char *cmd_buf, size_t cmd_buf_size,
                struct sockaddr_un *un, socklen_t *un_len)
{
#ifdef SCM_CREDENTIALS
    /* Read a message and control messages from 'fd'.  */
    iovec iov;
    iov.iov_base = cmd_buf;
    iov.iov_len = cmd_buf_size - 1;

    msghdr msg;
    char cred_buf[CMSG_SPACE(sizeof(ucred))];
    memset(&msg, 0, sizeof msg);
    msg.msg_name = un;
    msg.msg_namelen = sizeof *un;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cred_buf;
    msg.msg_controllen = sizeof cred_buf;

    ssize_t n = recvmsg(fd, &msg, 0);
    *un_len = msg.msg_namelen;
    if (n < 0)
    {
        return errno;
    }
    cmd_buf[n] = '\0';

    /* Ensure that the message has credentials ensuring that it was sent
     * from the same user who started us, or by root. */
    ucred* cred = NULL;
    for (cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
            cmsg = CMSG_NXTHDR(&msg, cmsg))
    {
        if (cmsg->cmsg_level == SOL_SOCKET
                && cmsg->cmsg_type == SCM_CREDENTIALS)
        {
            cred = (ucred *) CMSG_DATA(cmsg);
        }
        else if (cmsg->cmsg_level == SOL_SOCKET
                 && cmsg->cmsg_type == SCM_RIGHTS)
        {
            /* Anyone can send us fds.  If we don't close them, then that's
             * a DoS: the sender can overflow our fd table. */
            int* fds = (int *) CMSG_DATA(cmsg);
            size_t n_fds = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof *fds;
            size_t i;
            for (i = 0; i < n_fds; i++)
            {
                close(fds[i]);
            }
        }
    }
    if (!cred)
    {
        fprintf(stderr, "vlog: config message lacks credentials\n");
        return -1;
    }
    else if (cred->uid && cred->uid != getuid())
    {
        fprintf(stderr, "vlog: config message uid=%ld is not 0 or %ld\n",
                (long int) cred->uid, (long int) getuid());
        return -1;
    }

    return 0;
#else /* !SCM_CREDENTIALS */
    socklen_t len;
    ssize_t n;
    struct stat s;
    time_t recent;

    /* Receive a message. */
    len = sizeof *un;
    n = recvfrom(fd, cmd_buf, cmd_buf_size - 1, 0,
                 (sockaddr *) un, &len);
    *un_len = len;
    if (n < 0)
    {
        return errno;
    }
    cmd_buf[n] = '\0';

    len -= offsetof(sockaddr_un, sun_path);
    un->sun_path[len] = '\0';
    if (stat(un->sun_path, &s) < 0)
    {
        fprintf(stderr, "vlog: config message from inaccessible socket: %s\n",
                strerror(errno));
        return -1;
    }
    if (!S_ISSOCK(s.st_mode))
    {
        fprintf(stderr, "vlog: config message not from a socket\n");
        return -1;
    }
    recent = time(0) - 30;
    if (s.st_atime < recent || s.st_ctime < recent || s.st_mtime < recent)
    {
        fprintf(stderr, "vlog: config socket too old\n");
        return -1;
    }
    if (s.st_uid && s.st_uid != getuid())
    {
        fprintf(stderr, "vlog: config message uid=%ld is not 0 or %ld\n",
                (long int) s.st_uid, (long int) getuid());
        return -1;
    }
    return 0;
#endif /* !SCM_CREDENTIALS */
}

static void
vlog_server_fsm(Vlog& vlog, int fd)
{
    for (;;)
    {
        char cmd_buf[512];
        sockaddr_un un;
        socklen_t un_len;
        int error;

        error = recv_with_creds(fd, cmd_buf, sizeof cmd_buf, &un, &un_len);
        if (error > 0)
        {
            if (error != EAGAIN && error != EWOULDBLOCK)
            {
                fprintf(stderr, "vlog: reading configuration socket: %s",
                        strerror(errno));
            }
            break;
        }
        else if (error < 0)
        {
            continue;
        }

        /* Process message and send reply. */
        std::string reply = "nak";
        if (!strncmp(cmd_buf, "set ", 4))
        {
            try
            {
                reply = vlog.set_levels_from_string(cmd_buf + 4);
            }
            catch (const std::exception& e)
            {
                fprintf(stderr, "vlog: unable to set levels: %s\n", e.what());
            }

        }
        else if (!strcmp(cmd_buf, "list"))
        {
            reply = vlog.get_levels();
        }
        sendto(fd, reply.data(), reply.size(), 0, (sockaddr*) &un, un_len);
    }
    /* TODO: Callback the same function when fd is ready */
}

/* Client socket. */

Vlog_client_socket::Vlog_client_socket()
    : fd(-1)
{
}

Vlog_client_socket::~Vlog_client_socket()
{
    close();
}

/* Connects to a Vlog server socket.  If 'path' does not start with '/', then
 * it start with a PID as a string.  If a non-null, non-absolute name was
 * passed to Vlog_server_socket::listen(), then it must follow the PID in
 * 'path'.  If 'path' starts with '/', then it must be an absolute path that
 * gives the exact name of the Unix domain socket to connect to.
 *
 * Returns 0 if successful, otherwise a positive errno value. */
int
Vlog_client_socket::connect(const char* target)
{
    close();

    if (target[0] == '/')
    {
        path = target;
    }
    else
    {
        path = std::string("/tmp/vlogs.") + target;
    }

    std::string bind_addr(string_format("/tmp/vlog.%ld", (long int) getpid()));
    fd = make_unix_socket(false, false, &bind_addr, &path);
    return fd < 0 ? -fd : 0;
}

/* Disconnects from the server. */
void
Vlog_client_socket::close()
{
    if (fd >= 0)
    {
        ::close(fd);
        unlink(path.c_str());
        fd = -1;
    }
}

/* Sends 'request' to the server socket.  Returns 0 if successful, otherwise a
 * positive errno value. */
int
Vlog_client_socket::send(const char* request)
{
#ifdef SCM_CREDENTIALS
    struct ucred cred;
    cred.pid = getpid();
    cred.uid = getuid();
    cred.gid = getgid();

    iovec iov;
    iov.iov_base = (void*) request;
    iov.iov_len = strlen(request);

    char buf[CMSG_SPACE(sizeof cred)];
    msghdr msg;
    memset(&msg, 0, sizeof msg);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = buf;
    msg.msg_controllen = sizeof buf;

    cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_CREDENTIALS;
    cmsg->cmsg_len = CMSG_LEN(sizeof cred);
    memcpy(CMSG_DATA(cmsg), &cred, sizeof cred);
    msg.msg_controllen = cmsg->cmsg_len;

    ssize_t nbytes = ::sendmsg(fd, &msg, 0);
#else /* !SCM_CREDENTIALS */
    ssize_t nbytes = ::send(fd, request, strlen(request), 0);
#endif /* !SCM_CREDENTIALS */
    if (nbytes > 0)
    {
        return nbytes == strlen(request) ? 0 : ENOBUFS;
    }
    else
    {
        return errno;
    }
}

/* Attempts to receive a response from the server socket.  Sets 'error' to 0
 * if successful, otherwise to a positive errno value.  If successful, returns
 * the string returned. */
std::string
Vlog_client_socket::recv(int& error)
{
    pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    int nfds = poll(&pfd, 1, 1000);
    if (nfds == 0)
    {
        error = ETIMEDOUT;
        return "";
    }
    else if (nfds < 0)
    {
        error = errno;
        return "";
    }

    char buffer[65536];
    ssize_t nbytes = read(fd, buffer, sizeof buffer);
    if (nbytes < 0)
    {
        error = errno;
        return "";
    }
    else
    {
        error = 0;
        return std::string(buffer, nbytes);
    }
}

/* Sends 'request' to the server socket and waits for a reply.  Sets 'error' to
 * 0 if successful, otherwise to a positive errno value.  If successful,
 * returns the reply. */
std::string
Vlog_client_socket::transact(const char* request, int& error)
{
    /* Retry up to 3 times. */
    for (int i = 0; i < 3; ++i)
    {
        error = send(request);
        if (error)
        {
            return "";
        }
        std::string reply = recv(error);
        if (error != ETIMEDOUT)
        {
            return reply;
        }
    }
    return "";
}

std::string
Vlog_client_socket::target()
{
    return path;
}

/* Helper functions. */

/* Stores in '*un' a sockaddr_un that refers to file 'name'.  Stores in
 * '*un_len' the size of the sockaddr_un. */
static void
make_sockaddr_un(const std::string& name, sockaddr_un* un, socklen_t* un_len)
{
    un->sun_family = AF_UNIX;
    strncpy(un->sun_path, name.c_str(), sizeof un->sun_path);
    un->sun_path[sizeof un->sun_path - 1] = '\0';
    *un_len = (offsetof(struct sockaddr_un, sun_path)
               + strlen (un->sun_path) + 1);
}

Disposition remove_socket(std::string socket_name, const Event&)
{
    unlink(socket_name.c_str());

    return CONTINUE;
}

/* Creates a Unix domain datagram socket that is bound to '*bind_path' (if
 * 'bind_path' is non-null) and connected to '*connect_path' (if 'connect_path'
 * is non-null).  If 'nonblock' is true, the socket is made non-blocking.  If
 * 'passcred' is true, the socket is configured to receive SCM_CREDENTIALS
 * control messages.
 *
 * Returns the socket's fd if successful, otherwise a negative errno value. */
static int
make_unix_socket(bool nonblock, bool passcred,
                 const std::string* bind_path, const std::string* connect_path)
{
    int fd = socket(PF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        return -errno;
    }

    if (nonblock)
    {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1)
        {
            goto error;
        }
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        {
            goto error;
        }
    }

    if (bind_path)
    {
        sockaddr_un un;
        socklen_t un_len;
        make_sockaddr_un(*bind_path, &un, &un_len);
        if (unlink(un.sun_path) && errno != ENOENT)
        {
            fprintf(stderr, "unlinking \"%s\": %s\n",
                    un.sun_path, strerror(errno));
        }
        /* Remove bound socket just before the daemon exits, so as not to leave
         * debris in /tmp. */
        // TODO: fix this
        /*nox::register_handler(Shutdown_event::static_get_name(),
                              boost::bind(&remove_socket, *bind_path, _1),
                              9998);*/
        if (bind(fd, (sockaddr*) &un, un_len))
        {
            goto error;
        }
    }

    if (connect_path)
    {
        sockaddr_un un;
        socklen_t un_len;
        make_sockaddr_un(*connect_path, &un, &un_len);
        if (connect(fd, (sockaddr*) &un, un_len))
        {
            goto error;
        }
    }

#ifdef SCM_CREDENTIALS
    if (passcred)
    {
        int enable = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &enable, sizeof(enable)))
        {
            goto error;
        }
    }
#endif

    return fd;

error:
    int error = errno;
    close(fd);
    return -error;
}

} // namespace vigil
