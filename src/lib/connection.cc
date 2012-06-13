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
#include <config.h>
#include <inttypes.h>
#include <algorithm>
#include <sstream>
#include <utility>
#include <boost/asio/ssl.hpp>
#include <boost/bind.hpp>
#include <boost/exception/all.hpp>
#include <boost/foreach.hpp>
#include <boost/make_shared.hpp>
#include <boost/pool/object_pool.hpp>
#include <boost/timer.hpp>
#include "assert.hh"
#include "connection.hh"
#include "vlog.hh"

namespace vigil
{

namespace ba = ::boost::asio;
namespace bs = ::boost::system;

static Vlog_module lg("connection");

// Wrapper class template for handler objects to allow handler memory
// allocation to be customised. Calls to operator() are forwarded to the
// encapsulated handler.
template <typename Handler>
class custom_alloc_handler
{
public:
    custom_alloc_handler(handler_allocator& a, Handler h)
        : allocator_(a),
          handler_(h)
    {
    }

    template <typename Arg1>
    void operator()(Arg1 arg1)
    {
        handler_(arg1);
    }

    template <typename Arg1, typename Arg2>
    void operator()(Arg1 arg1, Arg2 arg2)
    {
        handler_(arg1, arg2);
    }

    friend void* asio_handler_allocate(std::size_t size,
                                       custom_alloc_handler<Handler>* this_handler)
    {
        return this_handler->allocator_.allocate(size);
    }

    friend void asio_handler_deallocate(void* pointer, std::size_t /*size*/,
                                        custom_alloc_handler<Handler>* this_handler)
    {
        this_handler->allocator_.deallocate(pointer);
    }

private:
    handler_allocator& allocator_;
    Handler handler_;
};

// Helper function to wrap a handler object to add custom allocation.
template <typename Handler>
inline custom_alloc_handler<Handler> make_custom_alloc_handler(
    handler_allocator& a, Handler h)
{
    return custom_alloc_handler<Handler>(a, h);
}

Connection::Connection()
{
}

void
Connection::close(const bs::error_code& ec)
{
    close_cb();
}

/* Constructs a Openflow connection that takes over ownership of 'stream'. */
template <typename Async_stream>
Stream_connection<Async_stream>::Stream_connection(
    boost::shared_ptr<Async_stream> stream)
    : Connection(), stream(stream), strand(stream->get_io_service()),
      tx_bytes(0), rx_bytes(0)
{
    ba::ip::tcp::socket::non_blocking_io non_blocking_io(true);
    stream->lowest_layer().io_control(non_blocking_io);
}

/* Close the stream associated with this connection */
template <typename Async_stream>
Stream_connection<Async_stream>::~Stream_connection()
{
    VLOG_DBG(lg, "flushed (tx: %zu, rx: %zu)", tx_bytes, rx_bytes);
}

template <typename Async_stream>
void
Stream_connection<Async_stream>::register_cb(
    Close_callback& ccb, Recv_callback& rcb, Send_callback& scb)
{
    close_cb = ccb;
    recv_cb = rcb;
    send_cb = scb;
}

template <typename Async_stream>
void
Stream_connection<Async_stream>::close(const boost::system::error_code& ec)
{
    Connection::close(ec);
    stream->lowest_layer().close();
}

template <typename Async_stream>
void
Stream_connection<Async_stream>::send(const ba::streambuf& buf)
{
    stream->async_write_some(buf.data(),
                             make_custom_alloc_handler(tx_allocator_,
                                                       strand.wrap(boost::bind(&Stream_connection::handle_send,
                                                               shared_from_this(),
                                                               ba::placeholders::error,
                                                               ba::placeholders::bytes_transferred))));
}

template <typename Async_stream>
void
Stream_connection<Async_stream>::recv(ba::mutable_buffers_1 buf)
{
    /*if (stream->lowest_layer().available() > 0) {
        bs::error_code ec;
        size_t bytes_transferred =
            stream->read_some(buf.prepare(buf.max_size() - buf.size()), ec);
        handle_recv(ec, bytes_transferred);
    } else {*/
    stream->async_read_some(buf,
                            make_custom_alloc_handler(rx_allocator_,
                                                      strand.wrap(boost::bind(&Stream_connection<Async_stream>::handle_recv,
                                                              shared_from_this(),
                                                              ba::placeholders::error,
                                                              ba::placeholders::bytes_transferred))));
    //}
}

template <typename Async_stream>
void
Stream_connection<Async_stream>::handle_recv(const bs::error_code& ec,
                                             const size_t& bytes_transferred)
{
    if (ec)
    {
        if (ec == ba::error::operation_aborted)
            return;
        VLOG_WARN(lg, "rx error (%s)", ec.message().c_str());
        close(ec);
        return;
    }
    rx_bytes += bytes_transferred;
    try
    {
        recv_cb(bytes_transferred);
    }
    catch (const std::exception& e)
    {
        VLOG_ERR(lg, "Exception in recv callback: %s", e.what());
        VLOG_ERR(lg, "Extra information:\n%s",
                 boost::current_exception_diagnostic_information().c_str());
        close(ec);
    }

}

template <typename Async_stream>
void
Stream_connection<Async_stream>::handle_send(const bs::error_code& ec,
                                             const size_t& bytes_transferred)
{
    if (ec)
    {
        if (ec == ba::error::operation_aborted)
            return;
        VLOG_WARN(lg, "tx error (%s)", ec.message().c_str());
        close(ec);
        return;
    }
    tx_bytes += bytes_transferred;
    try
    {
        send_cb(bytes_transferred);
    }
    catch (const std::exception& e)
    {
        VLOG_ERR(lg, "Exception in send callback: %s", e.what());
        VLOG_ERR(lg, "Extra information:\n%s",
                 boost::current_exception_diagnostic_information().c_str());
        close(ec);
    }

}

template <typename Async_stream>
std::string
Stream_connection<Async_stream>::to_string()
{
    std::stringstream ss;
    ss << stream->lowest_layer().local_endpoint()
       << "<->"
       << stream->lowest_layer().remote_endpoint();
    return ss.str();
}

typedef boost::asio::ip::tcp::socket tcp_socket;
typedef boost::asio::ssl::stream<tcp_socket> ssl_socket;

template class Stream_connection<ssl_socket>;
template class Stream_connection<tcp_socket>;

} // namespace vigil
