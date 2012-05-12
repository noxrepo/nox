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
#ifndef CONNECTION_MANAGER_HH
#define CONNECTION_MANAGER_HH 1

#include <inttypes.h>
#include <memory>
#include <string>
#include <vector>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/function.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/timer.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>
#include <boost/utility.hpp>

#include "component.hh"
#include "event.hh"

namespace vigil
{

typedef boost::asio::ip::tcp::socket tcp_socket;
typedef boost::asio::ssl::stream<tcp_socket> ssl_socket;

class Connection;

/* Connection manager component */
class Connection_manager
    : public Component
{
public:
    /* Construct a new component instance */
    static Component* instantiate(const Component_context*,
                                  const std::list<std::string>& interfaces);

    void configure();
    void install();

private:
    enum Conn_t { TCP, SSL, PTCP, PSSL, UNKNOWN };

    typedef boost::function<void()> Listen_callback;
    //typedef std::string Protocol_name;
    //typedef boost::unordered_map<uint16_t, Protocol_name> Port_protocol_map;

    // Port number to protocol name map
    // Use it to resolve the connection handler for each port number
    //Port_protocol_map port_map;

    // List of interfaces we are listening on
    std::list<std::string> interfaces;

    Connection_manager(const Component_context*,
                       const std::list<std::string>& interfaces);

    void parse(const std::string& interface,
               Conn_t& type,
               std::string& ip,
               uint16_t& port,
               std::string& key,
               std::string& cert,
               std::string& cafile);

    template <typename Async_stream>
    void handle_connect(boost::shared_ptr<Async_stream>,
                        Listen_callback,
                        const boost::system::error_code&);

    void handle_handshake(boost::asio::ssl::stream_base::handshake_type,
                          boost::shared_ptr<ssl_socket>,
                          Listen_callback, const boost::system::error_code&);

    void connect(Conn_t type, const std::string& host, const uint16_t& port,
                 const std::string& key,
                 const std::string& cert,
                 const std::string& cafile);

    void listen(boost::shared_ptr<boost::asio::ip::tcp::acceptor>);
    void listen(boost::shared_ptr<boost::asio::ip::tcp::acceptor>,
                const std::string& key,
                const std::string& cert,
                const std::string& cafile);
    void listen(Conn_t type, const std::string& bind_ip, const uint16_t& port,
                const std::string& key,
                const std::string& cert,
                const std::string& cafile);
};

} // namespace vigil

#endif
