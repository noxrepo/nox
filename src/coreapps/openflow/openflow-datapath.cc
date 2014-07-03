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
#include <netinet/in.h>
#include <endian.h>

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
#include <boost/thread/locks.hpp>

#include "assert.hh"
#include "openflow-event.hh"
#include "vlog.hh"
#include "of_nox.hh"

#include <iostream>
#include <stdlib.h>

namespace vigil
{
namespace openflow
{

namespace bp = ::boost::posix_time;
namespace ba = ::boost::asio;
namespace bs = ::boost::system;

static Vlog_module lg("openflow-datapath");

size_t hash_value(const Openflow_datapath& dp)
{
    boost::hash<datapathid> h;
    return h(dp.id());
}

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

Openflow_datapath::Openflow_datapath(Openflow_manager& mgr)
    : datapath_state(HANDSHAKE), handshake_state(HELLO), role(OFPCR_ROLE_EQUAL),
      manager(mgr),
      handshake_done(false), hello_received(false), features_req_sent(false),
      probe_interval(15),//, idle_timer(io_service),
      rx_buf(new ba::streambuf(512 * 1024)),
      tx_buf_active(new ba::streambuf(1024 * 1024)),
      tx_buf_pending(new ba::streambuf(1024 * 1024)),
      is_sending(false)
{
    recv_start = recv_buff;
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
    hello_received = false;
    features_req_sent = false;

    if (handshake_done)
    {
        Openflow_datapath_leave_event dple(shared_from_this());
        manager.dispatch(dple);
    }
}

void
Openflow_datapath::recv_cb(const size_t& bytes_transferred)
{
    rx_buf->commit(bytes_transferred);
    int msg_len = bytes_transferred;

    while(msg_len > 0)
    {
        int buff_len = recv_start - recv_buff;  
        int buffer_free = v13::OFP_MAX_MSG_BYTES - buff_len;
        int l = std::min(buffer_free, msg_len);
        
        rx_buf->sgetn(recv_start, l);
        recv_start += l;
        buff_len += l;
        msg_len -= l;
        while (1)
        {
            if (buff_len < v13::OFP_HEADER_BYTES)
            {
                break;
            }

            struct ofp_header* oh = (struct ofp_header *)recv_buff;
            uint32_t of_len = ntohs(oh->length);
            if (of_len > buff_len)
            {
                break;
            }

            handle_message((uint8_t*)recv_buff, of_len);
            memmove(recv_buff, recv_buff + of_len, buff_len - of_len);
            buffer_free -= of_len;
        }
    }

    connection->recv(rx_buf->prepare(rx_buf->max_size() - rx_buf->size())
    );
}

void
Openflow_datapath::send_cb(const size_t& bytes_transferred)
{
    boost::lock_guard<boost::mutex> lock(send_mutex);
    assert(is_sending);
    tx_buf_active->consume(bytes_transferred);
    lg.dbg("sent %zu remaining %zu %zu", bytes_transferred,
             tx_buf_active->size(), tx_buf_pending->size());

    if (tx_buf_active->size() == 0 && tx_buf_pending->size() > 0) {
        tx_buf_active.swap(tx_buf_pending);
        //oa_active.swap(oa_pending);
    }

    if (tx_buf_active->size() > 0)
        connection->send(*tx_buf_active);
    else
        is_sending = false;
}

int
Openflow_datapath::send_packet_out(uint32_t buf_id, void *buf,
                        int buf_size, uint32_t in_port, uint32_t out_port)
{
    enum ofputil_protocol protocol;
    protocol = ofputil_protocol_from_ofp_version(OFP13_VERSION);
    struct ofputil_packet_out po;

    po.buffer_id = buf_id;
    if (po.buffer_id == UINT32_MAX)
    {
        po.packet = buf;
        po.packet_len = buf_size;
    }
    else
    {
        po.packet = NULL;
        po.packet_len = 0;
    }
    po.in_port = in_port;

    uint64_t ofpacts_stub[64 / 8];
    struct ofpbuf ofpacts;
    ofpbuf_use_stack(&ofpacts, ofpacts_stub, sizeof(ofpacts_stub));
    if (out_port != OFPP_MAX && out_port != OFPP_NONE)
    {
        ofpact_put_OUTPUT(&ofpacts)->port = out_port;
    }
    ofpact_pad(&ofpacts);

    po.ofpacts = (struct ofpact *)ofpacts.data;
    po.ofpacts_len = ofpacts.size;

    struct ofpbuf *msg = ofputil_encode_packet_out(&po, protocol);
    int ret = send_of_buf(msg);
    dump_of_buf(msg);
    ofpbuf_delete(msg);
    return ret;
}

void
Openflow_datapath::dump_of_buf(struct ofpbuf *msg)
{
    char *s = ofp_to_string(msg->data, msg->size, 5);
    lg.dbg("ofp dump:%s\n", s);
    free(s);
}

/*
  * Warning : Maybe not thread safe
  */
size_t
Openflow_datapath::send_of_buf(struct ofpbuf *msg)
{
    ofpmsg_update_length(msg);
    boost::lock_guard<boost::mutex> lock(send_mutex);
    assert(msg->size <= v13::OFP_MAX_MSG_BYTES);

    /* Not enough space to send package */
    if (msg->size > tx_buf_pending->max_size() - tx_buf_pending->size()) {
        return 0;
    }
    else{
        tx_buf_pending->prepare(msg->size);
        void* temp = msg->data;
        tx_buf_pending->sputn(static_cast<const char*>(temp), msg->size);
    }

    /* send_cb will send all buffer */
    if (is_sending)
        return msg->size;

    if (tx_buf_active->size() == 0 && tx_buf_pending->size() > 0) {
        tx_buf_active.swap(tx_buf_pending);
    }

    if (tx_buf_active->size() > 0)
    {
        is_sending = true;
        connection->send(*tx_buf_active);
    }

    return msg->size;
}

void
Openflow_datapath::handle_message(uint8_t* data, const size_t len)
{
    //VLOG_DBG(lg, "received %s", msg->name());
    switch (datapath_state)
    {
    case IDLE:
        //transit_to(CONNECTED);
    case HANDSHAKE:
        //assert(msg->type in ...);
        //VLOG_WARN(lg, "%s: Unexpected message (type 0x%02"PRIx8") "
        //          "waiting for hello", to_string().c_str(), oh->type);
        //transit_to(S_DISCONNECTED);
        if (STOP == handle_handshake(data, len))
        {
            close();
        }
        break;
    case CONNECTED:
    {
        const struct ofp_header *oh = (struct ofp_header *)data;
        enum ofptype type;
        enum ofperr error;

        error = ofptype_decode(&type, oh);
        if (error)
        {
            lg.err("decode of failed: %d", error);
            close();
            break;
        }

        std::string name = get_eventname_from_type(type);
        lg.dbg("dispatch openflow event: %s", name.c_str());
        if (type == OFPTYPE_PACKET_IN)
        {
            Packet_in_event pie(shared_from_this(), name, oh);
            manager.dispatch(pie);
        }
        else
        {
            Openflow_event ofe(shared_from_this(), name, oh);
            manager.dispatch(ofe);
        }
        break;
    }
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
Openflow_datapath::handle_handshake(uint8_t* data, const size_t len)
{
    struct ofpbuf msg;
    ofpbuf_use_const(&msg, data, len);

    const struct ofp_header *oh = (struct ofp_header *)msg.data;
    enum ofptype type;
    enum ofperr error;

    error = ofptype_decode(&type, oh);
    if (error || type == OFPTYPE_ERROR)
    {
        lg.err("decode handshake of packet failed");
        return STOP;
    }

    if (type == OFPTYPE_FEATURES_REPLY)
    {
        if (!hello_received && !features_req_sent)
        {
            lg.err("handshake invalid feature reply");
            return STOP;
        }
        struct ofputil_switch_features features;
        struct ofpbuf b;

        error = ofputil_decode_switch_features(oh, &features, &b);
        if (error)
        {
            lg.err("handshake decode switch feature failed");
            return STOP;
        }
        id_ = datapathid::from_host(features.datapath_id);
        // TODO: fix this
        datapath_state = CONNECTED;
        lg.dbg("dispatch join event: %s", id_.string().c_str());
        Openflow_datapath_join_event dpje(shared_from_this());
        manager.dispatch(dpje);
        send_port_desc_request();
        handshake_done = true;
        return CONTINUE;
    }

    if (type == OFPTYPE_HELLO)
    {
        const struct ofp_header *oh = (struct ofp_header*)data;
        of_version = int(oh->version);
        if (of_version < OFP13_VERSION) {
            lg.warn("%s: negotiation failed %d",
                      id_.string().c_str(), of_version);
            send_hello_error();
            return STOP;
        }
        hello_received = true;
    }
    
    if (!features_req_sent)
    {
        lg.dbg("send feature request");
        send_hello();
        send_features_request();
        send_switch_config();
        features_req_sent = true; 
    }
    return CONTINUE;
}

int Openflow_datapath::send_common_request(enum ofpraw raw_type)
{
    struct ofpbuf *msg;
    msg = ofpraw_alloc(raw_type, OFP13_VERSION, 0);
    int ret = send_of_buf(msg);
    ofpbuf_delete(msg);
    return ret;
}

int Openflow_datapath::send_hello()
{
    struct ofpbuf *hello = ofputil_encode_hello(0x1e);   //ovs-ofctl encode-hello 0x1e OFPT_HELLO (OF1.3)
    int ret = send_of_buf(hello);
    ofpbuf_delete(hello);
    return ret;
}

int Openflow_datapath::send_hello_error()
{
    struct ofpbuf *b;
    b = ofperr_encode_hello(OFPERR_OFPHFC_INCOMPATIBLE, OFP13_VERSION, "just of 1.3");
    int ret = send_of_buf(b);
    ofpbuf_delete(b);
    return ret;
}

int Openflow_datapath::send_error(enum ofperr error, const struct ofp_header *request)
{
    struct ofpbuf *msg;
    msg = ofperr_encode_reply(error, request);
    int ret = send_of_buf(msg);
    return ret;
}

int Openflow_datapath::send_echo_request()
{
    return send_common_request(OFPRAW_OFPT_ECHO_REQUEST);
}

int Openflow_datapath::send_echo_reply(const Event& e)
{
    auto ofe = assert_cast<const Openflow_event&>(e);
    struct ofpbuf *reply = make_echo_reply(ofe.oh);
    return send_of_buf(reply);
}

int Openflow_datapath::send_port_desc_request()
{
    //return send_common_request(OFPRAW_OFPST_PORT_DESC_REQUEST);
    struct ofpbuf *msg;
    msg = ofpraw_alloc(OFPRAW_OFPST_PORT_DESC_REQUEST, OFP13_VERSION, 0);
    int ret = send_of_buf(msg);
    ofpbuf_delete(msg);
    return ret;
}

int Openflow_datapath::send_stats_request()
{
    return 0;
}

int Openflow_datapath::send_barrier_request()
{
    return 0;
}

int Openflow_datapath::send_features_request()
{
    return send_common_request(OFPRAW_OFPT_FEATURES_REQUEST);
}

int Openflow_datapath::send_switch_config()
{
    struct ofpbuf *msg;
    struct ofp_switch_config *ofpsc;
    msg = ofpraw_alloc(OFPRAW_OFPT_SET_CONFIG, OFP13_VERSION, 0);
    ofpsc = (struct ofp_switch_config*)ofpbuf_put_zeros(msg, sizeof *ofpsc);
    ofpsc->flags = OFPC_FRAG_NORMAL;
    ofpsc->miss_send_len = v13::OFP_MAX_MSG_BYTES - v13::OFP_HEADER_BYTES;
    ofpmsg_update_length(msg);
    int ret = send_of_buf(msg);
    ofpbuf_delete(msg);
    return ret;
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
