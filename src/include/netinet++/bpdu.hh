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
//=============================================================================
// Description of Bridge Protocol Data Unit (BPDU).  These are used by the
// Spanning Tree Protocol as defined in IEEE 802.1D-2004.
//
// Copyright (C) 2007 Nicira Networks
//=============================================================================
#ifndef BPDU_HH
#define BPDU_HH

#include <stdint.h>

#include "ethernetaddr.hh"
#include "static_lib.hh"

namespace vigil
{

//=============================================================================
// Bridge_id
//=============================================================================
struct Bridge_id
{
    uint16_t priority;
    ethernetaddr addr;

    uint16_t get_priority()
    {
        return ntohs(priority);
    }
    void set_priority(uint16_t in)
    {
        priority = htons(in);
    }

    bool operator == (const Bridge_id& in) const;
    bool operator != (const Bridge_id& in) const;
    bool operator < (const Bridge_id& in) const;
    bool operator <= (const Bridge_id& in) const;
    bool operator > (const Bridge_id& in) const;
    bool operator >= (const Bridge_id& in) const;
} __attribute__((__packed__));

inline
bool Bridge_id::operator== (const Bridge_id& in) const
{
    return ::memcmp(&in, this, sizeof(Bridge_id)) == 0;
}

inline
bool Bridge_id::operator!= (const Bridge_id& in) const
{
    return ::memcmp(&in, this, sizeof(Bridge_id)) != 0;
}

inline
bool Bridge_id::operator< (const Bridge_id& in) const
{
    return ::memcmp(&in, this, sizeof(Bridge_id)) < 0;
}

inline
bool Bridge_id::operator<= (const Bridge_id& in) const
{
    return ::memcmp(&in, this, sizeof(Bridge_id)) <= 0;
}

inline
bool Bridge_id::operator> (const Bridge_id& in) const
{
    return ::memcmp(&in, this, sizeof(Bridge_id)) > 0;
}

inline
bool Bridge_id::operator>= (const Bridge_id& in) const
{
    return ::memcmp(&in, this, sizeof(Bridge_id)) >= 0;
}

inline
std::ostream& operator<< (std::ostream& os, const Bridge_id& in)
{
    os << "0x" << std::hex << ntohs(in.priority) << "/" << in.addr;
    return os;
}


//=============================================================================
// Config_bpdu
//=============================================================================
struct Config_bpdu
{

    static const uint16_t STP_PROTO   = htons_<0x0000>::val;
    static const uint8_t  STP_VERSION = 0x00;
    static const uint8_t  TYPE_CONFIG = 0x00;  /* Configure message */
    static const uint8_t  TYPE_TC     = 0x80;  /* Topology change message */

    // Link costs as defined by STP
    static const uint32_t COST_10MB  = 100;
    static const uint32_t COST_100MB = 19;
    static const uint32_t COST_1GB   = 4;
    static const uint32_t COST_10GB  = 2;


    uint16_t get_proto_id() const
    {
        return ntohs(proto_id);
    }
    uint8_t  get_version() const
    {
        return version;
    }
    uint8_t  get_type() const
    {
        return type;
    }
    uint8_t  get_flags() const
    {
        return flags;
    }
    const Bridge_id& get_root_id() const
    {
        return root_id;
    }
    uint32_t get_root_cost() const
    {
        return ntohl(root_cost);
    }
    const Bridge_id& get_br_id() const
    {
        return br_id;
    }
    uint16_t get_port_id() const
    {
        return ntohs(port_id);
    }
    uint32_t get_msg_age() const
    {
        return ntohl(msg_age);
    }
    uint32_t get_max_age() const
    {
        return ntohl(max_age);
    }
    uint32_t get_hello_time() const
    {
        return ntohl(hello_time);
    }
    uint32_t get_fwd_delay() const
    {
        return ntohl(fwd_delay);
    }


    uint16_t  proto_id;
    uint8_t   version;
    uint8_t   type;
    uint8_t   flags;
    Bridge_id root_id;
    uint32_t  root_cost;
    Bridge_id br_id;
    uint16_t  port_id;
    uint16_t  msg_age;
    uint16_t  max_age;
    uint16_t  hello_time;
    uint16_t  fwd_delay;
} __attribute__((__packed__));

inline
std::ostream& operator<< (std::ostream& os, const Config_bpdu& in)
{
    os << "[proto:" << ntohs(in.proto_id)
       << " ver:" << in.version
       << " type:" << in.type
       << " flags:0x" << std::hex << ntohl(in.flags)
       << " root:" << in.root_id
       << " cost:" << ntohl(in.root_cost)
       << " br:" << in.br_id
       << " port:0x" << std::hex << ntohs(in.port_id)
       << " msg_age:" << ntohs(in.msg_age)
       << " max_age:" << ntohs(in.max_age)
       << " hello:" << ntohs(in.hello_time)
       << " delay:" << ntohs(in.fwd_delay)
       << "]";

    return os;
}

}

#endif /* BPDU_HH */
