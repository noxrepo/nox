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
//-----------------------------------------------------------------------------

#ifndef VLAN_HH
#define VLAN_HH

#include <netinet/in.h>

namespace vigil
{

struct vlan
{
    /* Constants are in host byte order */
    static const int VID_MASK = 0xfff;
    static const uint16_t PCP_MASK = 0xe000;
    static const int PCP_SHIFT = 13;  /* PCP is at [15:13] */

    /* Stored in network byte order */
    unsigned short tci;
    unsigned short encapsulated_proto;

    /* Parameters and return values are in host byte order */
    uint16_t id() const;
    void     set_id(uint16_t id);
    uint8_t  pcp() const;
    void     set_pcp(uint8_t pcp);

    uint16_t proto() const;

    static bool is_vlan(const ethernet& ether);

    uint8_t* data();

} __attribute__((__packed__));

//-----------------------------------------------------------------------------
inline
uint16_t
vlan::id() const
{
    return (ntohs(tci) & VID_MASK);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
uint8_t
vlan::pcp() const
{
    return ((ntohs(tci) & PCP_MASK) >> PCP_SHIFT);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
void
vlan::set_pcp(uint8_t pcp)
{
    tci = (tci & ~htons(PCP_MASK)) |
          htons((((uint16_t)pcp) << PCP_SHIFT) & PCP_MASK);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
void
vlan::set_id(uint16_t id)
{
    tci = (tci & ~htons(VID_MASK)) | htons(id & VID_MASK);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
uint16_t
vlan::proto() const
{
    return ntohs(encapsulated_proto);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool
vlan::is_vlan(const ethernet& ether)
{
    return ether.type == ethernet::VLAN;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
uint8_t*
vlan::data()
{
    return (((uint8_t*)this) + sizeof(struct vlan));
}
//-----------------------------------------------------------------------------

}

#endif  // -- VLAN_HH
