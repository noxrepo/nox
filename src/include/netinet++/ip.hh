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
// IPv4 packet
//
// -----------------------------------------------
// |version|  ihl  |  TOS      | total length    |
// ----------------------------------------------
// |    identification         |flags|frag offset|
// -----------------------------------------------
// |      TTL      |  Protocol | Header Checksum |
// -----------------------------------------------
// |           source ip_ address                 |
// -----------------------------------------------
// |        destination ip_ address               |
// -----------------------------------------------
// |         .... options and padding ...        |
// ...............................................
//
//
// TODO:
//
// ip(pcap_pkthdr*, uint8_t*, bool copy = false) <-- initialize from pcap packet
//
//-----------------------------------------------------------------------------

#ifndef NETINET_IP_HH
#define NETINET_IP_HH

#include "ipaddr.hh"

#include <string>
#include <sstream>

namespace vigil
{

//-----------------------------------------------------------------------------
struct ip_
{
    // ------------------------------------------------------------------------
    // STATIC CONST
    // ------------------------------------------------------------------------

    static const uint16_t ADDRLEN = sizeof(ipaddr);
    static const uint8_t  VER     = 4;

    static const uint16_t MAXPACKET = 65535;

    // Our default values
    static const uint8_t DEFPROTO = 0;

    // Internet implementation parameters
    static const uint8_t MAXTTL_ = 255;
    static const uint8_t DEFTTL  = 64;  // default TTL, RFC 1340
    static const uint8_t FRAGTTL = 60;  // time to live for frags
    static const uint8_t TTLDEC  = 1;   // subtracted when forwarding

    struct proto
    {
        static const uint8_t IP = 0;        // Dummy protocol for TCP
        static const uint8_t HOPOPTS = 0;   // IPv6 Hop-by-Hop options.
        static const uint8_t ICMP = 1;      // Internet Control Message Protocol.
        static const uint8_t IGMP = 2;      // Internet Group Management Protocol.
        static const uint8_t IPIP = 4;      // IPIP tunnels (older KA9Q tunnels use 94).
        static const uint8_t TCP = 6;       // Transmission Control Protocol.
        static const uint8_t EGP = 8;       // Exterior Gateway Protocol.
        static const uint8_t PUP = 12;      // PUP protocol.
        static const uint8_t UDP = 17;      // User Datagram Protocol.
        static const uint8_t IDP = 22;      // XNS IDP protocol.
        static const uint8_t TP = 29;       // SO Transport Protocol Class 4.
        static const uint8_t IPV6 = 41;     // IPv6 header.
        static const uint8_t ROUTING = 43;  // IPv6 routing header.
        static const uint8_t FRAGMENT = 44; // IPv6 fragmentation header.
        static const uint8_t RSVP = 46;     // Reservation Protocol.
        static const uint8_t GRE = 47;      // General Routing Encapsulation.
        static const uint8_t ESP_PROTO = 50;      // encapsulating security payload.
        static const uint8_t AH = 51;       // authentication header.
        static const uint8_t ICMPV6 = 58;   // ICMPv6.
        static const uint8_t NONE = 59;     // IPv6 no next header.
        static const uint8_t DSTOPTS = 60;  // IPv6 destination options.
        static const uint8_t MTP = 92;      // Multicast Transport Protocol.
        static const uint8_t ENCAP = 98;    // Encapsulation Header.
        static const uint8_t PIM = 103;     // Protocol Independent Multicast.
        static const uint8_t COMP = 108;    // Compression Header Protocol.
        static const uint8_t SCTP = 132;    // Stream Control Transmission Protocol.
        static const uint8_t RAW = 255;     // Raw IP packets.
    };


    static const uint8_t ETHHELLO = 138;

    // TOS
    static const uint8_t TOS_MIN_DELAY = 0x10;
    static const uint8_t TOS_MAX_THROUGHPUT = 0x08;
    static const uint8_t TOS_MAX_RELIABILITY = 0x04;
    static const uint8_t TOS_MIN_MONETARY_COST = 0x02;

    // FLAGS
    static const uint32_t DONT_FRAGMENT = 0x4000;
    static const uint32_t MORE_FRAGMENTS = 0x2000;
    static const uint32_t FRAG_OFF_MASK = 0x1fff;
    static bool is_fragment(uint32_t frag_off)
    {
        return frag_off & htons(MORE_FRAGMENTS | FRAG_OFF_MASK);
    }

    // ------------------------------------------------------------------------
    // Typedefs
    // ------------------------------------------------------------------------

    typedef ipaddr            AddrType;

    // ------------------------------------------------------------------------
    // String Representation
    // ------------------------------------------------------------------------

    std::string string()   const;
    const char* c_string() const;

    // ------------------------------------------------------------------------
    // ------------------------------------------------------------------------

    uint16_t        calc_csum();
    static uint16_t checksum(void *, size_t);

    uint16_t  ihl:4;
    uint16_t  ver:4;
    uint8_t   tos;
    uint16_t  tot_len;
    uint16_t  id;
    uint16_t  frag_off;
    uint8_t   ttl;
    uint8_t   protocol;
    uint16_t  csum;

    ipaddr  saddr;
    ipaddr  daddr;

    ip_();
    ip_(const ip_&);

    uint8_t* data();

} __attribute__ ((__packed__));
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ip_::ip_()
{
    ihl = 5;
    ver = 4;

    tos     = 0;
    tot_len = 0;

    id = 0;
    frag_off = 0;

    ttl = DEFTTL;

    protocol = 0;
    csum     = 0;

    protocol = DEFPROTO;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ip_::ip_(const ip_& in)
{
    ihl      = in.ihl;
    ver      = in.ver;
    tos      = in.tos;
    tot_len  = in.tot_len;
    id       = in.id;
    frag_off = in.frag_off;
    ttl      = in.ttl;
    protocol = in.protocol;
    csum     = in.csum;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
uint16_t
ip_::calc_csum()
{
    uint16_t oldsum = csum;
    csum = 0;
    uint16_t newsum = checksum(this, ihl*4);
    csum = oldsum;
    return newsum;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Internet checksum where size is number of bytes
//-----------------------------------------------------------------------------
inline
uint16_t
ip_::checksum(void * in, size_t size)
{
    register size_t nleft = size;
    const u_short *w = (u_short *) in;
    register u_short answer;
    register int sum = 0;

    while (nleft > 1)
    {
        sum += *w++;
        nleft -= 2;
    }

    if (nleft == 1)
    {
        sum += htons(*(u_char *) w << 8);
    }

    sum = (sum >> 16) + (sum & 0xffff); // add high 16 to low 16
    sum += (sum >> 16); // add carry
    answer = ~sum;
    return answer;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
std::string
ip_::string() const
{
    std::ostringstream o;

    o << "[("   << saddr.string() << " > " << daddr.string() << ")"
      << std::dec
      << " ihl:"  << (int)ihl
      << " ver:"  << (int)ver
      << " tos:"  << (int)tos
      << " len:"  << (int)ntohs(tot_len)
      << std::hex
      << " id:"    << (int)ntohs(id)
      << std::dec
      << " frag:"  << (int)ntohs(frag_off)
      << " ttl:"   << (int)ttl
      << " prot: " << (int)protocol
      << std::hex
      << " sum:" << (int)csum
      << "]";

    return o.str();
}
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
inline
const char*
ip_::c_string() const
{
    static char buf[256];
    ::strncpy(buf,this->string().c_str(), 256);
    return buf;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
uint8_t*
ip_::data()
{
    return (((uint8_t*)this) + (ihl * 4));
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
std::ostream&
operator <<(std::ostream& os,const ip_& iph)
{
    os << iph.string();
    return os;
}
//-----------------------------------------------------------------------------

}

#endif // -- NETINET_IP_HH
