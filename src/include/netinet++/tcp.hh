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
// File:  tcp.hh
// Date:  Fri Mar 21 23:27:04 PST 2003
// Author: Martin Casado, Norman Franke
//
//-----------------------------------------------------------------------------

#ifndef TCP_HH
#define TCP_HH

#include "ipaddr.hh"

#include <stdint.h>
#include <assert.h>

namespace vigil
{

//-----------------------------------------------------------------------------
struct tcp
{

    static const uint32_t NODELAY          = 1; // Don't delay send to coalesce packets
    static const uint32_t MAXSEG           = 2; // Set maximum segment size
    static const uint32_t CORK         = 3; // Control sending of partial frames
    static const uint32_t KEEPIDLE         = 4; // Start keeplives after this period
    static const uint32_t KEEPINTVL        = 5; // Interval between keepalives
    static const uint32_t KEEPCNT          = 6; // Number of keepalives before death
    static const uint32_t SYNCNT           = 7; // Number of SYN retransmits
    static const uint32_t LINGER2          = 8; // Life time of orphaned FIN-WAIT-2 state
    static const uint32_t INFO         = 11; // Information about this connection.
    static const uint32_t QUICKACK         = 12; // Bock/reenable quick ACKs.
    static const uint32_t DEFER_ACCEPT = 9;// Wake up listener only when data arrive
    static const uint32_t WINDOW_CLAMP = 10; // Bound advertised window

    static const uint32_t MSS = 512;
    static const uint32_t MAXWIN = 65535; // largest value for unscaled window
    static const uint32_t MAX_WINSHIFT = 14; // maximum window shirt

    static const uint8_t DEF_PORT = 0;

    static const uint8_t FIN  = 0x01;
    static const uint8_t SYN  = 0x02;
    static const uint8_t RST  = 0x04;
    static const uint8_t PUSH = 0x08;
    static const uint8_t ACK  = 0x10;
    static const uint8_t URG  = 0x20;

    uint16_t sport;             // source port
    uint16_t dport;             // destination port
    uint32_t seq;               // sequence number
    uint32_t ack;               // acknowledgement number

#  if __BYTE_ORDER == __LITTLE_ENDIAN
    uint8_t x2:4;               // (unused)
    uint8_t off:4;              // data offset
#  elif __BYTE_ORDER == __BIG_ENDIAN
    uint8_t off:4;              // data offset
    uint8_t x2:4;               // (unused)
#  else
#  error " BYTE ORDERING not specified "
#  endif

    uint8_t  flags;
    uint16_t win;               // window
    uint16_t check;             // checksum
    uint16_t urp;               // urgent pointer

    uint16_t len()
    {
        return ((uint16_t) off) * 4;
    };

    uint8_t* data();

    static uint16_t checksum(ipaddr sip, ipaddr dip, void *tcp,
                             uint16_t tcp_len);
    uint16_t        calc_csum(ipaddr sip, ipaddr dip, uint16_t datalen);
    //TODO: versions for IPv6

} __attribute__ ((__packed__)); // -- struct tcp
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
uint8_t*
tcp::data()
{
    return (((uint8_t*)this) + (off * 4));
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
uint16_t
tcp::calc_csum(ipaddr sip, ipaddr dip, uint16_t datalen)
{
    uint16_t oldcheck = check;
    check = 0;
    uint16_t newcheck = checksum(sip, dip, this, len() + datalen);
    check = oldcheck;
    return newcheck;
}
//-----------------------------------------------------------------------------

inline
uint16_t
tcp::checksum(ipaddr sip, ipaddr dip, void *tcp, uint16_t tcp_len)
{
    uint32_t sum = 0;
    uint16_t *data = (uint16_t*)tcp;

    sum += (uint16_t)(sip.addr >> 16);
    sum += (uint16_t)sip.addr;
    sum += (uint16_t)(dip.addr >> 16);
    sum += (uint16_t)dip.addr;
    sum += htons(0x0006); //proto
    sum += htons(tcp_len);
    while(tcp_len > 1)
    {
        sum += *data++;
        tcp_len -= sizeof(uint16_t);
    }
    if (tcp_len)
    {
        sum += (*((uint8_t*)data)) << 16;
    }

    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);

    return uint16_t(~sum);
}
//-----------------------------------------------------------------------------

}

#endif  //-- TCP_HH
