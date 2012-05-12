/* Copyright 2008 (C) Nicira, Inc.
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
/* Remote logging control socket support for vlog.
 *
 * This extends control over logging levels in vlog to external processes
 * across a Unix domain socket.  Primarily used by the vlogconf utility. */

#ifndef VLOG_SOCKET_HH
#define VLOG_SOCKET_HH 1

#include <boost/noncopyable.hpp>
#include <string>

namespace vigil
{

class Vlog;

/* Server control connection for a Vlog. */
class Vlog_server_socket
    : boost::noncopyable
{
public:
    Vlog_server_socket(Vlog&);
    ~Vlog_server_socket();
    int listen(const char* path = 0);
    void close();
private:
    std::string path;
    Vlog& vlog;
    int fd;
};

/* Client control connection to a Vlog. */
class Vlog_client_socket
    : boost::noncopyable
{
public:
    Vlog_client_socket();
    ~Vlog_client_socket();
    int connect(const char* path);
    void close();
    int send(const char* request);
    std::string recv(int& error);
    std::string transact(const char* request, int& error);
    std::string target();
private:
    std::string path;
    int fd;
};

} // namespace vigil

#endif /* vlog-socket.hh */
