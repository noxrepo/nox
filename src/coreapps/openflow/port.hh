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
#ifndef PORT_HH
#define PORT_HH 1

#include <iostream>
#include "netinet++/ethernetaddr.hh"


namespace vigil {

struct Port
{
    Port(const ofputil_phy_port *opp);
    Port(const Port& port);
    Port() : port_no(0), speed(0), config(0), state(0),
             curr(0), advertised(0), supported(0), peer(0) {};

    uint32_t port_no;
    std::string name;
    uint32_t speed;
    uint32_t config;
    uint32_t state;

    /*  Bitmaps of OFPPF_* that describe features.  All bits disabled if
     * unsupported or unavailable. */
    uint32_t curr;
    uint32_t advertised;
    uint32_t supported;
    uint32_t peer;
    
    uint32_t   curr_speed;    /* Current port bitrate in kbps. */
    uint32_t   max_speed;     /* Max port bitrate in kbps */

    ethernetaddr hw_addr;

    Port& operator=(const Port& p)
    {
        port_no = p.port_no;
        name = p.name;
        speed = p.speed;
        config = p.config;
        curr = p.curr;
        advertised = p.advertised;
        supported = p.supported;
        peer = p.peer;
        curr_speed = p.curr_speed;
        max_speed = p.max_speed;
        hw_addr.set_octet(p.hw_addr);
        return *this;
    }
};

inline
Port::Port(const Port& port):
    port_no(port.port_no), name(port.name), speed(port.speed),
    config(port.config), state(port.state), curr(port.curr),
    advertised(port.advertised), supported(port.supported), peer(port.peer),
    curr_speed(port.curr_speed), max_speed(port.max_speed)
{
    hw_addr.set_octet(port.hw_addr);
}

inline
Port::Port(const ofputil_phy_port *opp) : name((char *)opp->name)
{
    port_no    = opp->port_no;
    config     = opp->config;
    state      = opp->state;
    curr       = opp->curr;
    advertised = opp->advertised;
    supported  = opp->supported;
    peer       = opp->peer;
    curr_speed = opp->curr_speed;
    max_speed = opp->max_speed;
    hw_addr.set_octet(opp->hw_addr);

    if (curr & (OFPPF_10MB_HD | OFPPF_10MB_FD)) {
        speed = 10;
    } else if (curr & (OFPPF_100MB_HD | OFPPF_100MB_FD)) {
        speed = 100;
    } else if (curr & (OFPPF_1GB_HD | OFPPF_1GB_FD)) {
        speed = 1000;
    } else if (curr & OFPPF_10GB_FD) {
        speed = 10000;
    } else {
        speed = 0;
    }
}

inline
std::ostream& operator<< (std::ostream& stream, const Port& p)
{
    stream << p.port_no << "(" << p.name << "): " << p.hw_addr
                << " speed: " << p.speed
                << " config: " << std::hex << p.config
                << " state: " << std::hex << p.state
                << " curr: " << std::hex << p.curr
                << " advertised: " << std::hex << p.advertised
                << " supported: " << std::hex << p.supported
                << " peer: " << std::hex << p.peer
                << "curr_speed: "<< std::hex << p.curr_speed
                << "max_speed: "<< std::hex << p.max_speed;
    return stream;
}

} // namespace vigil

#endif /* PORT_HH */
