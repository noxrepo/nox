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
//-----------------------------------------------------------------------------
// Description:
//
// Provide a fuzzy c++ interface for handeling ethernet packets
//
//-----------------------------------------------------------------------------

#ifndef ETHERNET_HH
#define ETHERNET_HH

#include <iostream>
#include <stdint.h>

#include "ip.hh"
#include "ethernetaddr.hh"
#include "static_lib.hh"

namespace vigil
{

//-----------------------------------------------------------------------------
struct ethernet
{
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------

    typedef  ethernetaddr     AddrType;

    static const unsigned int ADDRLEN =   AddrType::LEN;

    static const unsigned int   ETHER_LEN = 14;
    static const  unsigned int  PAYLOAD_MIN =   6;

    //-------------------------------------------------------------------------
    // from net/ethernet.h
    //
    // #define ETHERTYPE_PUP           0x0200          /* Xerox PUP */
    // #define ETHERTYPE_IP            0x0800          /* IP */
    // #define ETHERTYPE_ARP           0x0806          /* Address resolution */
    // #define ETHERTYPE_REVARP        0x8035          /* Reverse ARP */
    //-------------------------------------------------------------------------

    static const uint16_t PUP    = htons_<0x0200>::val;
    static const uint16_t IP     = htons_<0x0800>::val;
    static const uint16_t ARP    = htons_<0x0806>::val;
    static const uint16_t REVARP = htons_<0x8035>::val;
    static const uint16_t VLAN   = htons_<0x8100>::val;
    static const uint16_t IPV6   = htons_<0x86dd>::val;
    static const uint16_t LLDP   = htons_<0x88cc>::val;
    static const uint16_t PAE    = htons_<0x888e>::val;

    /* Values below this cutoff are 802.3 packets and the two bytes
     * following MAC addresses are used as a frame length.  Otherwise, the
     * two bytes are used as the Ethernet type.
     */
    static const uint16_t ETH2_CUTOFF = 0x0600;

    ethernetaddr  daddr;    // destination ethernet address
    ethernetaddr  saddr;    // source ethernet address
    uint16_t      type;     // packet type ID

    ethernet();
    ethernet(const ethernet&);

    uint8_t* data();

    std::string type_str();
    static std::string type_str(uint16_t type);

} __attribute__((__packed__));
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ethernet::ethernet()
{
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ethernet::ethernet(const ethernet& in)
{
    daddr = in.daddr;
    saddr = in.saddr;
    type  = in.type;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
uint8_t*
ethernet::data()
{
    return (((uint8_t*)this) + sizeof(struct ethernet));
}
//-----------------------------------------------------------------------------

inline
std::string
ethernet::type_str()
{
    return type_str(ntohs(type));
}

inline
std::string
ethernet::type_str(uint16_t type)
{
    if (type < 0x05DC)
    {
        return "IEEE 802.3 length";
    }
    if (type >= 0x0101 && type <= 0x01FF)
    {
        return "Experimental";
    }
    switch (type)
    {
    case 0x0200:
        return("XEROX PUP");
    case 0x0600:
        return("XEROX NS IDP");
    case 0x0660: /* fall through */
    case 0x0661:
        return("DLOG");
    case 0x0800:
        return("IP");
    case 0x0801:
        return("X.75 Internet");
    case 0x0802:
        return("NBS Internet");
    case 0x0803:
        return("ECMA Internet");
    case 0x0804:
        return("Chaosnet");
    case 0x0805:
        return("X.25 Level 3");
    case 0x0806:
        return("ARP");
    case 0x0808:
        return("Frame Relay ARP [RFC1701]");
    case 0x6559:
        return("Raw Frame Relay [RFC1701]");
    case 0x8035:
        return("REVARP");
    case 0x8037:
        return("Novell Netware IPX");
    case 0x809B:
        return("EtherTalk");
    case 0x80D5:
        return("IBM SNA Services over Ethernet");
    case 0x80F3:
        return("AARP");
    case 0x8100:
        return("VLAN");
    case 0x8137:
        return("IPX");
    case 0x814C:
        return("SNMP");
    case 0x86DD:
        return("IPv6");
    case 0x8808:
        return("Link Flow Control");
    case 0x880B:
        return("PPP");
    case 0x880C:
        return("GSMP");
    case 0x8847:
        return("MPLS (unicast)");
    case 0x8848:
        return("MPLS (multicast)");
    case 0x8863:
        return("PPPoE (Discovery)");
    case 0x8864:
        return("PPPoE (Session)");
    case 0x88BB:
        return("LWAPP");
    case 0x88CC:
        return("LLDP");
    case 0x888E:
        return("PAE (EAPOL)");
    case 0x9000:
        return("Loopback");
    case 0x9100: /* fall through */
    case 0x9200:
        return("VLAN Tag Protocol Identifier");
    case 0xFFFF:
        return("reserved");
    default:
        return("Unknown");
    }
}

}


#endif  // ETHERNET_HH
