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
#ifndef CONNECTION_HH
#define CONNECTION_HH 1

#include <string>
#include <boost/aligned_storage.hpp>
#include <boost/asio.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/function.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>

namespace vigil
{

// Class to manage the memory to be used for handler-based custom allocation.
// It contains a single block of memory which may be returned for allocation
// requests. If the memory is in use when an allocation request is made, the
// allocator delegates allocation to the global heap.
class handler_allocator
    : private boost::noncopyable
{
public:
    handler_allocator()
	{
		in_use_ = false;
	}

    void* allocate(std::size_t size)
    {
		if (!in_use_ && size < storage_.size)
		{
			in_use_ = true;
			return storage_.address();
		}
		return ::operator new(size);
    }

    void deallocate(void* pointer)
    {
		if (pointer == storage_.address())
		{
			in_use_ = false;
			return;
		}
		::operator delete(pointer);
    }

private:
	std::size_t count;

    // Storage space used for handler-based custom memory allocation.
    boost::aligned_storage<1024> storage_;

    // Whether the handler-based custom allocation storage has been used.
    bool in_use_;
};

/* Abstract class for a connection */
class Connection
    : public boost::enable_shared_from_this<Connection>,
  private boost::noncopyable
{
public:
    typedef boost::function<void()> Close_callback;
    typedef boost::function<void(const size_t&)> Recv_callback;
    typedef boost::function<void(const size_t&)> Send_callback;

    Connection();
    virtual ~Connection() {}

    virtual void register_cb(Close_callback&, Recv_callback&, Send_callback&) = 0;
    virtual void close(const boost::system::error_code&);
    virtual void send(const boost::asio::streambuf&) = 0;
    virtual void recv(boost::asio::mutable_buffers_1) = 0;

    virtual std::string to_string() = 0;

protected:
    Close_callback close_cb;
    Recv_callback recv_cb;
    Send_callback send_cb;

    handler_allocator rx_allocator_;
    handler_allocator tx_allocator_;
};

/* Wrapper for carrying traffic over a stream connection,
 * e.g. TCP or SSL. */
template <typename Async_stream>
class Stream_connection
    : public Connection
{
public:
    Stream_connection(boost::shared_ptr<Async_stream>);
    ~Stream_connection();

    boost::shared_ptr<Stream_connection> shared_from_this()
    {
        return boost::static_pointer_cast<Stream_connection>(
                   Connection::shared_from_this());
    }
    boost::shared_ptr<Stream_connection const> shared_from_this() const
    {
        return boost::static_pointer_cast<Stream_connection const>(
                   Connection::shared_from_this());
    }

    virtual void register_cb(Close_callback&, Recv_callback&, Send_callback&);

    virtual void close(const boost::system::error_code&);
    virtual void send(const boost::asio::streambuf&);
    virtual void recv(boost::asio::mutable_buffers_1);

    virtual std::string to_string();

private:
    boost::shared_ptr<Async_stream> stream;
    boost::asio::strand strand;
    size_t tx_bytes;
    size_t rx_bytes;

    void handle_recv(const boost::system::error_code&, const size_t&);
    void handle_send(const boost::system::error_code&, const size_t&);
};

} // namespace vigil

#endif
