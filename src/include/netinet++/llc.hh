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

#ifndef LLC_HH
#define LLC_HH

#include "ethernet.hh"

namespace vigil
{

struct Llc
{
    static const unsigned int LLC_LEN = 3;
    static const unsigned int ETHER_LLC_LEN = ethernet::ETHER_LEN + LLC_LEN;

    // A "type" field value of less than 0x0600 indicates that this
    // frame should be interpreted as an LLC packet, and the "type"
    // field should be interpreted as the frame's length.
    static const uint16_t LLC_CUTOFF = htons_<0x0600>::val;

    static bool is_llc(const ethernet& ether);
    uint8_t* data();

    uint8_t  dsap;     // Destination sap
    uint8_t  ssap;     // Source sap
    uint8_t  ctrl;     // Control
} __attribute__ ((__packed__));


inline
bool
Llc::is_llc(const ethernet& ether)
{
    return ether.type < LLC_CUTOFF;
}

inline
uint8_t*
Llc::data()
{
    return (((uint8_t*)this) + sizeof(struct Llc));
}

}

#endif  // -- LLC_HH
