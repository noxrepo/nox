/* Copyright 2009 (C) Nicira, Inc.
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
// Provide a fuzzy c++ interface for handeling eapol frames
//
//-----------------------------------------------------------------------------

#ifndef EAPOL_HH
#define EAPOL_HH

#include <iostream>
#include <stdint.h>

#include "ethernet.hh"

namespace vigil
{

//-----------------------------------------------------------------------------

/*
 * EAPOL Header Format (see IEEE 802.1X-2004):
 *
 * Octet 0: Protocol version (1 or 2).
 * Octet 1: Packet type:
 *   0 = EAP packet
 *   1 = EAPOL-Start
 *   2 = EAPOL-Logoff
 *   3 = EAPOL-Key
 *   4 = EAPOL-Encapsulated-ASF-Alert
 * Octets 2-3: Length of packet body field (0 if packet body is absent)
 * Octets 4-end: Packet body (present only for packet types 0, 3, 4)
 */
struct eapol
{
    static const unsigned int MIN_LEN = 4;

    static const uint8_t V1_PROTO = 1;
    static const uint8_t V2_PROTO = 2;

    static const uint8_t EAP_PACKET_TYPE = 0;
    static const uint8_t EAPOL_START_TYPE = 1;
    static const uint8_t EAPOL_LOGOFF_TYPE = 2;
    static const uint8_t EAPOL_KEY_TYPE = 3;
    static const uint8_t EAPOL_ENCAPSULATED_ASF_ALERT = 4;

    uint8_t  version;
    uint8_t  type;
    uint16_t body_len;

    eapol();
    eapol(const eapol&);

    uint8_t* data();

    static bool is_eapol(const ethernet& ether);

    std::string type_str();
    static std::string type_str(uint8_t type);

} __attribute__((__packed__));
//-----------------------------------------------------------------------------

inline
eapol::eapol()
{
}

inline
eapol::eapol(const eapol& in)
{
    version = in.version;
    type = in.type;
    body_len  = in.body_len;
}

inline
uint8_t*
eapol::data()
{
    return (((uint8_t*)this) + sizeof(struct eapol));
}

inline
bool
eapol::is_eapol(const ethernet& ether)
{
    return ether.type == ethernet::PAE;
}

inline
std::string
eapol::type_str()
{
    return type_str(type);
}

inline
std::string
eapol::type_str(uint8_t type)
{
    switch (type)
    {
    case EAP_PACKET_TYPE:
        return("EAP Packet");
    case EAPOL_START_TYPE:
        return("EAPOL-Start");
    case EAPOL_LOGOFF_TYPE:
        return("EAPOL-Logoff");
    case EAPOL_KEY_TYPE:
        return("EAPOL-Key");
    case EAPOL_ENCAPSULATED_ASF_ALERT:
        return("EAPOL-Encapsulated-ASF-Alert");
    default:
        return("Unknown");
    }
}

} //namespace vigil

#endif  // EAPOL_HH
