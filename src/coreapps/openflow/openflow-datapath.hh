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
#include "network_iarchive.hh"
#include "network_oarchive.hh"
#include <openflow/openflow-1.0.hh>

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

    void close() const;
    void send(const v1::ofp_msg*);

    bool operator==(const Openflow_datapath& that) const
    {
        return id_ == that.id_;
    }
    bool operator!=(const Openflow_datapath& that) const
    {
        return id_ != that.id_;
    }

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

    /* Underlying connection to the datapath */
    Openflow_manager& manager;
    boost::shared_ptr<Connection> connection;

    // ofp_header to hold the header temporarily
    bool header_set;
    bool hello_received;
    bool features_req_sent;
    v1::ofp_msg ofm;
    v1::ofp_features_reply features;

    // ID of joining switch
    datapathid id_;

    // Number of seconds before probing an idle datapath
    int probe_interval;
    //boost::asio::deadline_timer idle_timer;

    // Send and receive buffers
    boost::mutex tx_mutex;
    std::unique_ptr<boost::asio::streambuf> rx_buf;
    std::unique_ptr<boost::asio::streambuf> tx_buf_active;
    std::unique_ptr<boost::asio::streambuf> tx_buf_pending;
    std::unique_ptr<network_oarchive> oa;
    std::unique_ptr<network_oarchive> oa_active;
    network_iarchive ia;
    bool is_sending;

    void close_cb();
    void recv_cb(const size_t&);
    void send_cb(const size_t&);

    void handle_message(const v1::ofp_msg* msg);
    Disposition handle_disconnect(const Event&);
    Disposition handle_error_msg(const Event&);
    Disposition handle_handshake(const Event&);

    /*
    void check_idle(const boost::system::error_code& =
        boost::system::error_code());
    */
    std::string to_string();
};

std::size_t hash_value(const Openflow_datapath&);

} // namespace openflow
} // namespace vigil

#endif
