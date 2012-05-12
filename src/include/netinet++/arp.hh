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

#ifndef ARP_HH
#define ARP_HH

#include "static_lib.hh"
#include "ethernet.hh"
#include "ipaddr.hh"

namespace vigil
{

struct arp
{
    // -- Conform to model of Ethernet_type
    static const uint16_t ETHTYPE = ethernet::ARP;

    //-------------------------------------------------------------------------
    // STATIC CONST
    //-------------------------------------------------------------------------

    // ARP types
    static const uint16_t REQUEST   =      htons_<1>::val;
    static const uint16_t REPLY     =      htons_<2>::val;
    static const uint16_t REVERSEREQUEST = htons_<3>::val;
    static const uint16_t REVERSEREPLY   = htons_<4>::val;


    // ARP protocol HARDWARE identifiers.
    static const uint16_t NETROM   =  htons_<0>::val; // From KA9Q: NET/ROM pseudo.
    static const uint16_t ETHER    =  htons_<1>::val; // Ethernet 10/100Mbps.
    static const uint16_t EETHER   =  htons_<2>::val; // Experimental Ethernet.
    static const uint16_t AX25     =  htons_<3>::val; // AX.25 Level 2.
    static const uint16_t PRONET   =  htons_<4>::val; // PROnet token ring.
    static const uint16_t CHAOS    =  htons_<5>::val; // Chaosnet.
    static const uint16_t IEEE802  =  htons_<6>::val; // IEEE 802.2 Ethernet/TR/TB.
    static const uint16_t ARCNET   =  htons_<7>::val; // ARCnet.
    static const uint16_t APPLETLK =  htons_<8>::val; // APPLEtalk.
    static const uint16_t DLCI     =  htons_<15>::val; // Frame Relay DLCI.
    static const uint16_t ATM      =  htons_<19>::val; // ATM.
    static const uint16_t METRICOM =  htons_<23>::val; // Metricom STRIP (new IANA id).
    static const uint16_t IEEE995    = htons_<24>::val; // IEEE 1394.1995.
    static const uint16_t MAPOS      = htons_<25>::val; // MAPOS.
    static const uint16_t TWINAXIAL  = htons_<26>::val; // Twinaxial.
    static const uint16_t EUI64      = htons_<27>::val; // EUI-64.
    static const uint16_t HIPARP     = htons_<28>::val; // HIPARP.
    static const uint16_t ISO78163   = htons_<29>::val; // IP and ARP over ISO 7816-3.
    static const uint16_t ARPSEC     = htons_<30>::val; // ARPSec.
    static const uint16_t IPSEC      = htons_<31>::val; // IPsec tunnel.
    static const uint16_t INFINIBAND = htons_<32>::val; // Infiniband./ Last Modified

    // Dummy types for non ARP hardware
    static const uint16_t SLIP    = htons_<256>::val;
    static const uint16_t CSLIP   = htons_<257>::val;
    static const uint16_t SLIP6   = htons_<258>::val;
    static const uint16_t CSLIP6  = htons_<259>::val;
    static const uint16_t RSRVD   = htons_<260>::val; // Notional KISS type.
    static const uint16_t ADAPT   = htons_<264>::val;
    static const uint16_t ROSE    = htons_<270>::val;
    static const uint16_t X25     = htons_<271>::val; // CCITT X.25.
    static const uint16_t HWX25   = htons_<272>::val; // Boards with X.25 in firmware.
    static const uint16_t PPP     = htons_<512>::val;
    static const uint16_t CISCO   = htons_<513>::val; // Cisco HDLC.
    static const uint16_t HDLC    = htons_<513>::val;
    static const uint16_t LAPB    = htons_<516>::val; // LAPB.
    static const uint16_t DDCMP   = htons_<517>::val; // Digital's DDCMP.
    static const uint16_t RAWHDLC = htons_<518>::val; // Raw HDLC.

    static const uint16_t TUNNEL     =  htons_<768>::val; // IPIP tunnel.
    static const uint16_t TUNNEL6    =  htons_<769>::val; // IPIP6 tunnel.
    static const uint16_t FRAD       =  htons_<770>::val; // Frame Relay Access Device.
    static const uint16_t SKIP       =  htons_<771>::val; // SKIP vif.
    static const uint16_t LOOPBACK   =  htons_<772>::val; // Loopback device.
    static const uint16_t LOCALTLK   =  htons_<773>::val; // Localtalk device.
    static const uint16_t FDDI       =  htons_<774>::val; // Fiber Distributed Data Interface.
    static const uint16_t BIF        =  htons_<775>::val; // AP1000 BIF.
    static const uint16_t SIT        =  htons_<776>::val; // sit0 device - IPv6-in-IPv4.
    static const uint16_t IPDDP      =  htons_<777>::val; // IP-in-DDP tunnel.
    static const uint16_t IPGRE      =  htons_<778>::val; // GRE over IP.
    static const uint16_t PIMREG     =  htons_<779>::val; // PIMSM register interface.
    static const uint16_t HIPPI      =  htons_<780>::val; // High Performance Parallel I'face.
    static const uint16_t ASH        =  htons_<781>::val; // (Nexus Electronics) Ash.
    static const uint16_t ECONET     =  htons_<782>::val; // Acorn Econet.
    static const uint16_t IRDA       =  htons_<783>::val; // Linux-IrDA.
    static const uint16_t FCPP       =  htons_<784>::val; // Point to point fibrechanel.
    static const uint16_t FCAL       =  htons_<785>::val; // Fibrechanel arbitrated loop.
    static const uint16_t FCPL       =  htons_<786>::val; // Fibrechanel public loop.
    static const uint16_t FCPFABRIC  =  htons_<787>::val; // Fibrechanel fabric.
    static const uint16_t IEEE802_TR =  htons_<800>::val; // Magic type ident for TR.
    static const uint16_t IEEE80211  =  htons_<801>::val; // IEEE 802.11.

    uint16_t  hrd;  // -- format of hardware address.
    uint16_t  pro;  // -- format of protocol address.
    uint8_t   hln;  // -- length of hardware address.
    uint8_t   pln;  // -- length of protocol address.
    uint16_t  op;   // -- arp opcode (command).

    struct ethernetaddr sha; // Sender hardware address
    struct ipaddr       sip; // Sender IP address
    struct ethernetaddr tha; // Target hardware address
    struct ipaddr       tip; // Target IP address

    arp();
    arp(const arp& in);

} __attribute__ ((__packed__)); // -- arp
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
arp::arp()
{ }
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
arp::arp(const arp& in)
{
    hrd = in.hrd;
    pro = in.pro;
    hln = in.hln;
    pln = in.pln;
    op  = in.op;

    sha = in.sha;
    sip = in.sip;
    tha = in.tha;
    tip = in.tip;
}
//-----------------------------------------------------------------------------

}

#endif  // -- ARP_HH
