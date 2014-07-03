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

#ifndef OPENFLOW_CONNECTION_HH
#define OPENFLOW_CONNECTION_HH 1

#include <boost/asio/streambuf.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/shared_ptr.hpp>

#include "connection.hh"
#include "openflow-manager.hh"
#include "netinet++/datapathid.hh"

namespace vigil
{
namespace openflow
{

class Openflow_manager;

class Openflow_datapath
    : public boost::enable_shared_from_this<Openflow_datapath>,
      boost::noncopyable
{
public:
    Openflow_datapath(Openflow_manager&);
    
    ~Openflow_datapath();

    void set_connection(boost::shared_ptr<Connection>);

    const datapathid& id() const
    {
        return id_;
    }

    bool operator==(const Openflow_datapath& that) const
    {
        return id_ == that.id_;
    }
    bool operator!=(const Openflow_datapath& that) const
    {
        return id_ != that.id_;
    }

    void set_role(enum ofp_controller_role new_role)
    {
        role = new_role;
    }

    enum ofp_controller_role get_role(){
        return role;
    }

    bool is_master()
    {
        return role == OFPCR_ROLE_MASTER;
    }

    bool is_slave()
    {
        return role == OFPCR_ROLE_SLAVE;
    }

    const std::string get_remote_ip(void)
    {
        return connection->get_remote_ip();
    }

    void close() const;
    
    size_t send_of_buf(struct ofpbuf *msg);

    void dump_of_buf(struct ofpbuf *msg);

    int send_echo_reply(const Event& e);
    
    int send_packet_out(uint32_t buf_id, void *buf, 
                            int buf_size, uint32_t in_port, uint32_t out_port);

private:
    void close_cb();

    void recv_cb(const size_t&);

    void send_cb(const size_t&);
    
    Disposition handle_handshake(uint8_t* data, const size_t len);

private:
    int send_common_request(enum ofpraw raw_type);
    
    int send_features_request();

    int send_switch_config();

    int send_stats_request();

    int send_port_desc_request();

    int send_echo_request();

    int send_barrier_request();

    int send_hello();

    int send_hello_error();

    int send_error(enum ofperr error, const struct ofp_header *request);

    void handle_message(uint8_t* data, const size_t len);

private:
    enum Datapath_state
    {
        /* This is the ordinary progression of states. */
        HANDSHAKE,              /* OpenFlow handshake in progress. */
        CONNECTED,              /* Connection established. */
        IDLE,                   /* Nothing received in a while. */

        /* These states are entered only when something goes wrong. */
        ERROR,                  /* Sending OFPT_ERROR message. */
        DISCONNECTED,
        DATAPATH_NSTATES
    } datapath_state;

    enum Handshake_state
    {
        HELLO,
        SEND_FEATURES_REQ,
        SEND_CONFIG,
        RECV_FEATURES_REPLY,
        HANDSHAKE_DONE,
        HANDSHAKE_NSTATES
    } handshake_state;

    static std::string datapath_state_desc[DATAPATH_NSTATES];
    static std::string handshake_state_desc[HANDSHAKE_NSTATES];

private:
    enum ofp_controller_role role;

    Openflow_manager& manager;
    boost::shared_ptr<Connection> connection;

    bool handshake_done;

    bool hello_received;
    bool features_req_sent;

    int of_version;

    datapathid id_;

    int probe_interval;
    //boost::asio::deadline_timer idle_timer;

    boost::mutex tx_mutex;
    std::unique_ptr<boost::asio::streambuf> rx_buf;
    std::unique_ptr<boost::asio::streambuf> tx_buf_active;
    std::unique_ptr<boost::asio::streambuf> tx_buf_pending;

    bool is_sending;

    boost::mutex send_mutex;
    
    char recv_buff[v13::OFP_MAX_MSG_BYTES];

    char* recv_start;
};

size_t hash_value(const Openflow_datapath&);

} // namespace openflow
} // namespace vigil

#endif
