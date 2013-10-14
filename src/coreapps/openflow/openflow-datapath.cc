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

#include "openflow-datapath.hh"

#include <config.h>
#include <iostream>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/function.hpp>
#include <boost/functional/hash.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/make_shared.hpp>
#include <boost/timer.hpp>

#include "assert.hh"
#include "openflow-datapath-join-event.hh"
#include "openflow-datapath-leave-event.hh"
#include "openflow-event.hh"
#include "vlog.hh"

namespace vigil
{
namespace openflow
{

namespace bp = ::boost::posix_time;
namespace ba = ::boost::asio;
namespace bs = ::boost::system;

static Vlog_module lg("openflow-datapath");

std::string
Openflow_datapath::datapath_state_desc[Openflow_datapath::DATAPATH_NSTATES] =
{
    "handshake",
    "connected",
    "idle",
    "error",
    "disconnected"
};

std::string
Openflow_datapath::handshake_state_desc[Openflow_datapath::HANDSHAKE_NSTATES] =
{
    "sending hello",
    "sending features request",
    "sending switch config",
    "receiving features reply",
    "checking switch auth"
};

size_t hash_value(const Openflow_datapath& dp)
{
    boost::hash<datapathid> h;
    return h(dp.id());
}

Openflow_datapath::Openflow_datapath(Openflow_manager& mgr)
    : datapath_state(HANDSHAKE), handshake_state(HELLO), manager(mgr),
      header_set(false), hello_received(false), features_req_sent(false),
      probe_interval(15),//, idle_timer(io_service),
      rx_buf(new ba::streambuf(512 * 1024)),
      tx_buf_active(new ba::streambuf(1024 * 1024)),
      tx_buf_pending(new ba::streambuf(1024 * 1024)),
      oa_active(new network_oarchive(*tx_buf_active)),
      oa_pending(new network_oarchive(*tx_buf_pending)),
      ia(*rx_buf),
      is_sending(false)
{
    /*
    manager.register_handler("ofp_error_msg",
            boost::bind(&Openflow_datapath::handle_error_msg, shared_from_this(), _1));
    manager.register_handler("ofp_features_reply",
            boost::bind(&Openflow_datapath::handle_handshake, shared_from_this(), _1));
    manager.register_handler("ofp_hello",
            boost::bind(&Openflow_datapath::handle_handshake, shared_from_this(), _1));
    */
}

Openflow_datapath::~Openflow_datapath()
{
    VLOG_ERR(lg, "Datapath deleted");
}

void
Openflow_datapath::set_connection(boost::shared_ptr<Connection> conn)
{
    connection = conn;
    Connection::Close_callback ccb =
        boost::bind(&Openflow_datapath::close_cb, shared_from_this());
    Connection::Recv_callback rcb =
        boost::bind(&Openflow_datapath::recv_cb, shared_from_this(), _1);
    Connection::Send_callback scb =
        boost::bind(&Openflow_datapath::send_cb, shared_from_this(), _1);
    connection->register_cb(ccb, rcb, scb);

    connection->recv(
        //ba::buffer(raw_rx_buf, sizeof raw_rx_buf)
        rx_buf->prepare(rx_buf->max_size() - rx_buf->size())
    );
}

void
Openflow_datapath::close() const
{
    boost::system::error_code ec;
    connection->close(ec);
}

void
Openflow_datapath::close_cb()
{
}

void
Openflow_datapath::recv_cb(const size_t& bytes_transferred)
{
    VLOG_DBG(lg, "recv %zu", bytes_transferred);
    rx_buf->commit(bytes_transferred);
    // Process all the fully received messages
    while (rx_buf->size() >= v1::OFP_HEADER_BYTES)
    {
        if (!header_set)
        {
            ofm.clear();
            ia >> ofm;
            header_set = true;
        }

        assert(ofm.length() != 0 && ofm.length() <= v1::OFP_MAX_MSG_BYTES);

        // Have not yet received the whole message
        if (rx_buf->size() < ofm.length() - v1::OFP_HEADER_BYTES)
        {
            break;
        }

        header_set = false;

        // Raw memory to construct ofp* object in place.
        char raw_buf[v1::OFP_MAX_MSG_BYTES];
        v1::ofp_msg* msg = reinterpret_cast<v1::ofp_msg*>(raw_buf);

        ofm.factory(ia, msg);

        handle_message(msg);
    }

    connection->recv(
        rx_buf->prepare(rx_buf->max_size() - rx_buf->size())
    );
}

void
Openflow_datapath::send_cb(const size_t& bytes_transferred)
{
    assert(is_sending);
    tx_buf_active->consume(bytes_transferred);
    VLOG_DBG(lg, "sent %zu remaining %zu %zu", bytes_transferred,
             tx_buf_active->size(), tx_buf_pending->size());

    if (tx_buf_active->size() == 0 && tx_buf_pending->size() > 0) {
        tx_buf_active.swap(tx_buf_pending);
        oa_active.swap(oa_pending);
    }

    if (tx_buf_active->size() > 0)
        connection->send(*tx_buf_active);
    else
        is_sending = false;
}

size_t
Openflow_datapath::send(const v1::ofp_msg* msg)
{
    VLOG_DBG(lg, "sending %s", msg->name());
    assert(msg->length() <= v1::OFP_MAX_MSG_BYTES);
    
    // Return 0 if not enough space in the buffer
    if (msg->length() > tx_buf_pending->max_size() - tx_buf_pending->size()) {
        return 0;
    }

    const_cast<v1::ofp_msg*>(msg)->factory(*oa_pending, NULL);

    if (is_sending)
        return msg->length();

    if (tx_buf_active->size() == 0 && tx_buf_pending->size() > 0) {
        tx_buf_active.swap(tx_buf_pending);
        oa_active.swap(oa_pending);
    }

    if (tx_buf_active->size() > 0)
    {
        is_sending = true;
        connection->send(*tx_buf_active);
    }

    return msg->length();
}

void
Openflow_datapath::handle_message(const v1::ofp_msg* msg)
{
    VLOG_DBG(lg, "received %s", msg->name());
    Openflow_event ofe(*this, msg);
    switch (datapath_state)
    {
    case IDLE:
        //transit_to(CONNECTED);
    case HANDSHAKE:
        //assert(msg->type in ...);
        //VLOG_WARN(lg, "%s: Unexpected message (type 0x%02"PRIx8") "
        //          "waiting for hello", to_string().c_str(), oh->type);
        //transit_to(S_DISCONNECTED);
        handle_handshake(ofe);
    case CONNECTED:
        manager.dispatch(ofe);
        break;
    case DISCONNECTED:
        break;
    default:
        //VLOG_DBG(lg,"%s: Invalid state %d in handle_recv",
        //         to_string().c_str(), state);
        //transit_to(DISCONNECTED);
        break;
    }
}

Disposition
Openflow_datapath::handle_disconnect(const Event& e)
{
    header_set = false;
    hello_received = false;
    features_req_sent = false;
    return STOP;
}

Disposition
Openflow_datapath::handle_error_msg(const Event& e)
{
    /*
    auto ofe = assert_cast<const Openflow_event&>(e);
    auto oer = assert_cast<const v1::ofp_error_msg*>(ofe.msg);

    */
    return STOP;
}

Disposition
Openflow_datapath::handle_handshake(const Event& e)
{
    auto ofe = assert_cast<const Openflow_event&>(e);
    if (ofe.dp != *this)
        return CONTINUE;
    const uint8_t& type = ofe.msg->type();
    if (type == v1::ofp_msg::OFPT_ERROR)
    {
        //auto oe = assert_cast<const v1::ofp_error_msg*>(ofe.msg);
    }
    else if (type == v1::ofp_msg::OFPT_FEATURES_REPLY)
    {
        if (!hello_received && !features_req_sent)
        {
            return STOP;
        }
        auto ofe = assert_cast<const Openflow_event&>(e);
        auto ofr = assert_cast<const v1::ofp_features_reply*>(ofe.msg);

        features = *ofr;
        id_ = datapathid::from_host(features.datapath_id());

        // TODO: fix this
        datapath_state = CONNECTED;
        Openflow_datapath_join_event dpje(shared_from_this());
        manager.dispatch(dpje);
    }
    else if (type == v1::ofp_msg::OFPT_HELLO)
    {
        const v1::ofp_hello* oh = assert_cast<const v1::ofp_hello*>(ofe.msg);
        if (oh->length() > v1::OFP_HELLO_BYTES)
        {
            VLOG_WARN(lg, "Extra-long hello (%u extra bytes)",
                      oh->length() - v1::OFP_HELLO_BYTES);
        }

        if (oh->version() == v1::OFP_VERSION)
        {
            VLOG_WARN(lg, "Negotiated OpenFlow version 0x%02x", oh->version());
            // send feat req
            v1::ofp_features_request fr;
            v1::ofp_set_config sc;
            send(oh);
            send(&fr);
            send(&sc);

            hello_received = true;
            features_req_sent = true;
        }
        else
        {
            VLOG_WARN(lg, "Version negotiation failed: we support "
                      "version 0x%02x but peer supports no later than"
                      "version 0x%02x", v1::OFP_VERSION, oh->version());
            // TODO: send an error with type OFPET_HELLO_FAILED and code
            // OFPHFC_INCOMPATIBLE
        }
    }
    return STOP;
}

#if 0
void
Openflow_datapath::check_idle(const bs::error_code& ec)
{
    if (ec)
    {
        return;
    }
    else if (idle_timer.elapsed() >= probe_interval)
    {
        if (state == CONNECTED)
        {
            VLOG_DBG(lg, "%s: Idle %d seconds, sending inactivity probe",
                     to_string().c_str(), probe_interval);
            idle_timer.reset();
            send_echo_request(boost::bind(&Connection::transit_to,
                                          shared_from_this(), IDLE, _1));
        }
        else
        {
            if (state == IDLE)
            {
                VLOG_WARN(lg,
                          "%s: No response to inactivity probe after %d seconds",
                          to_string().c_str(), probe_interval);
            }
            else if (state == HANDSHAKE)
            {
                VLOG_WARN(lg,
                          "%s: Handshake did not complete after %d seconds",
                          to_string().c_str(), probe_interval);
            }
            transit_to(DISCONNECTED);
        }
    }
    //idle_timer.expires_from_now(boost::posix_time::seconds(probe_interval) - idle_timer.elapsed());
    //idle_timer.async_wait(boost::bind(&Openflow_datapath::check_idle,
    //                             shared_from_this(), _1));
}

{
case HANDSHAKE:
    check_idle();
    send_hello();
case IDLE:
case CONNECTED:
    break;
case SEND_ERROR:
    send_error();
    break;
case DISCONNECTED:
    if (prev == DISCONNECTED)
        break;
    idle_timer.cancel();
    VLOG_WARN(lg, "%s: Disconnecting ", to_string().c_str());
    close();
    break;
default:
    VLOG_ERR(lg, "%s: BUG: unknown state %d!", to_string().c_str(), state);
    transit_to(DISCONNECTED);
}
}

#endif

} // namespace openflow
} // namespace vigil
