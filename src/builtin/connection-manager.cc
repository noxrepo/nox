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

#include "connection-manager.hh"

#include <config.h>
#include <algorithm>
#include <list>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <boost/algorithm/string.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>

#include "assert.hh"
#include "component.hh"
#include "connection.hh"
#include "event-dispatcher.hh"
#include "new-connection-event.hh"
#include "vlog.hh"

#include "kernel.hh"

namespace vigil
{

namespace ba = ::boost::asio;
namespace bs = ::boost::system;
namespace baip = ::boost::asio::ip;
namespace bassl = ::boost::asio::ssl;

static Vlog_module lg("connection_manager");

static const std::string conn_t_str[] = { "tcp", "ssl", "ptcp", "pssl" };

Connection_manager::Connection_manager(const Component_context* ctxt,
                                       const std::list<std::string>& interfaces)
    : Component(ctxt),
      interfaces(interfaces)
{
}

Component*
Connection_manager::instantiate(const Component_context* ctxt,
                                const std::list<std::string>& interfaces)
{
    return new Connection_manager(ctxt, interfaces);
}

void
Connection_manager::configure()
{
    /*
    std::list<std::string> handler_list;
    handler_list = ctxt->get_config_list("handlers");
    BOOST_FOREACH (const Protocol_name& name, handler_list)
    {
    uint16_t port = ctxt->get_config<uint16_t>("handlers." + name);
    port_map[port] = name;
    }
    */
}

void
Connection_manager::install()
{
    /* Bind/listen to interfaces */
    BOOST_FOREACH (const std::string& interface, interfaces)
    {
        Conn_t type;
        std::string host, key, cert, cafile;
        uint16_t port;

        parse(interface, type, host, port, key, cert, cafile);
        /*
        if (port_map.find(port) == port_map.end()) {
            std::stringstream ss;
            ss << "Port number \"" << port
        	    << "\" not defined in the configuration file.";
            throw std::runtime_error(ss.str());
        }
        Protocol_name pname = port_map[port];
        std::string handler_component_name = pname + "-connection-handler";
        // TODO: fix this and add assertions
        ctxt->resolve_by_name(handler_component_name);
        Component* component = ...
        if (component == NULL)
        {
           VLOG_DBG(lg, "-->%s", handler_component_name.c_str());
            throw std::runtime_error("No handler for the \"" + pname
        	    + "\" protocol.");
        }
        */
        if (type == TCP || type == SSL)
        {
            VLOG_DBG(lg, "connecting to %s:%s:%d:%s:%s:%s",
                     conn_t_str[type].c_str(), host.c_str(), port,
                     key.c_str(), cert.c_str(), cafile.c_str());
            connect(type, host, port, key, cert, cafile);
        }
        else if (type == PTCP || type == PSSL)
        {
            VLOG_DBG(lg, "listening on %s:%s:%d:%s:%s:%s",
                     conn_t_str[type].c_str(), host.c_str(), port,
                     key.c_str(), cert.c_str(), cafile.c_str());
            listen(type, host, port, key, cert, cafile);
        }
        else
        {
            throw std::runtime_error("Unsupported connection type \""
                                     + conn_t_str[type] + "\"");
        }
    }

    //throw std::runtime_error("ssl connection name not in the form ssl:HOST:[PORT]:KEY:CERT:CAFILE");
    //throw std::runtime_error("ptcp connection name not in a form like ptcp:[IP]:[PORT]");
    //throw std::runtime_error("pssl connection name not in a form like pssl:[IP]:[PORT]:KEY:CERT:CAFILE");
}

void
Connection_manager::parse(const std::string& interface,
                          Conn_t& type,
                          std::string& host,
                          uint16_t& port,
                          std::string& key,
                          std::string& cert,
                          std::string& cafile)
{
    using boost::lexical_cast;
    using boost::bad_lexical_cast;

    type = UNKNOWN;
    host = "0.0.0.0";
    port = 6633;
    key = "";
    cert = "";
    cafile = "";

    std::vector<std::string> tokens;
    boost::split(tokens, interface, boost::is_any_of(":"));
    size_t ntokens = tokens.size();
    if (ntokens >= 1)
    {
        if (tokens[0] == "tcp")
            type = TCP;
        else if (tokens[0] == "ssl")
            type = SSL;
        else if (tokens[0] == "ptcp")
            type = PTCP;
        else if (tokens[0] == "pssl")
            type = PSSL;

        if (ntokens == 2)
        {
            if (!tokens[1].empty())
                port = lexical_cast<uint16_t>(tokens[1]);
        }
        else
        {
            if (!tokens[1].empty())
                host = tokens[1];
            if (!tokens[2].empty())
                port = lexical_cast<uint16_t>(tokens[2]);
            if (ntokens > 3)
                key = tokens[3];
            if (ntokens > 4)
                cert = tokens[4];
            if (ntokens > 5)
                cafile = tokens[5];
        }
    }
}

template <typename Async_stream>
void
Connection_manager::handle_connect(boost::shared_ptr<Async_stream> socket,
                                   Listen_callback cb,
                                   const boost::system::error_code& ec)
{
    if (ec != ba::error::operation_aborted)
    {
        cb();
    }
    if (ec)
    {
        return;
    }

    // resolve the handler component based on port number
    // either the local or remote port is known to us
    /*
    uint16_t port = socket->lowest_layer().local_endpoint().port();
    if (port_map.find(port) == port_map.end())
    port = socket->lowest_layer().remote_endpoint().port();
    Protocol_name& pname = port_map[port];
    std::string handler_component_name = pname + "-connection-handler";
    Component* component = ctxt->get_by_name(handler_component_name);
    Connection_handler* ch = dynamic_cast<Connection_handler*>(component);
    */

    // create the connection helper class
    baip::tcp::no_delay nodelay(true);
    socket->lowest_layer().set_option(nodelay);
    boost::shared_ptr<Connection> connection(
        new Stream_connection<Async_stream>(socket));

    VLOG_WARN(lg, "connected: %s", connection->to_string().c_str());

    dispatch(New_connection_event(connection));
    // register the connection with protocol-specific connection handler
    //ch->register_connection(connection);
}

void
Connection_manager::handle_handshake(bassl::stream_base::handshake_type type,
                                     boost::shared_ptr<ssl_socket> socket,
                                     Listen_callback cb, const boost::system::error_code& ec)
{
    if (ec && ec != ba::error::operation_aborted)
    {
        return;
    }

    socket->async_handshake(type,
                            boost::bind(&Connection_manager::handle_connect<ssl_socket>,
                                        this, socket, cb, _1));
}

void
Connection_manager::connect(Conn_t type,
                            const std::string& host, const uint16_t& port,
                            const std::string& key,
                            const std::string& cert,
                            const std::string& cafile)
{
    ba::io_service& io = event_dispatcher->get_io_service();

    baip::tcp::endpoint peer(baip::address::from_string(host), port);

    Listen_callback cb;

    if (type == SSL)
    {
        bassl::context ssl_context(io, bassl::context::sslv23);
        ssl_context.set_options(bassl::context::default_workarounds
                                | bassl::context::no_sslv2);
        ssl_context.set_verify_mode(
            bassl::context::verify_peer |
            bassl::context::verify_fail_if_no_peer_cert |
            bassl::context::verify_client_once);
        ssl_context.load_verify_file(cafile);
        ssl_context.use_certificate_file(cert, bassl::context::pem);
        ssl_context.use_private_key_file(key, bassl::context::pem);

        boost::shared_ptr<ssl_socket> socket(new ssl_socket(io, ssl_context));
        socket->lowest_layer().async_connect(peer,
                                             boost::bind(&Connection_manager::handle_handshake,
                                                     this, bassl::stream_base::client, socket, cb, _1));
    }
    else if (type == TCP)
    {
        boost::shared_ptr<tcp_socket> socket(new tcp_socket(io));
        socket->async_connect(peer,
                              boost::bind(&Connection_manager::handle_connect<tcp_socket>,
                                          this, socket, cb, _1));
    }
}

void
Connection_manager::listen(boost::shared_ptr<boost::asio::ip::tcp::acceptor> acceptor)
{
    ba::io_service& io = acceptor->get_io_service();

    Listen_callback cb(
        boost::bind(&Connection_manager::listen, this, acceptor));
    boost::shared_ptr<tcp_socket> socket(new tcp_socket(io));
    acceptor->async_accept(socket->lowest_layer(),
                           boost::bind(&Connection_manager::handle_connect<tcp_socket>,
                                       this, socket, cb, _1));
}

void
Connection_manager::listen(boost::shared_ptr<boost::asio::ip::tcp::acceptor> acceptor,
                           const std::string& key,
                           const std::string& cert,
                           const std::string& cafile)
{
    Listen_callback cb(boost::bind(&Connection_manager::listen, this,
                                   acceptor, key, cert, cafile));

    ba::io_service& io = acceptor->get_io_service();

    bassl::context ssl_context(io, bassl::context::sslv23);
    ssl_context.set_options(bassl::context::default_workarounds
                            | bassl::context::no_sslv2);
    ssl_context.set_verify_mode(
        bassl::context::verify_peer |
        bassl::context::verify_fail_if_no_peer_cert |
        bassl::context::verify_client_once);
    if (!cafile.empty())
        ssl_context.load_verify_file(cafile);
    if (!cert.empty())
        ssl_context.use_certificate_file(cert, bassl::context::pem);
    if (!key.empty())
        ssl_context.use_private_key_file(key, bassl::context::pem);

    boost::shared_ptr<ssl_socket> socket(new ssl_socket(io, ssl_context));
    acceptor->async_accept(socket->lowest_layer(),
                           boost::bind(&Connection_manager::handle_handshake,
                                       this, bassl::stream_base::server, socket, cb, _1));
}

void
Connection_manager::listen(Conn_t type,
                           const std::string& bind_ip, const uint16_t& port,
                           const std::string& key,
                           const std::string& cert,
                           const std::string& cafile)
{
    ba::io_service& io = event_dispatcher->get_io_service();

    baip::address bind_address(baip::address::from_string(bind_ip));
    boost::shared_ptr<baip::tcp::acceptor> acceptor(new baip::tcp::acceptor(
                io, baip::tcp::endpoint(bind_address, port)));

    if (type == PSSL)
    {
        listen(acceptor, key, cert, cafile);
    }
    else if (type == PTCP)
    {
        listen(acceptor);
    }
}

} // namespace vigil
