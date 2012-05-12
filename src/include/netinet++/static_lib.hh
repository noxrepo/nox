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
// File:
// Date:
//
// Description:
//-----------------------------------------------------------------------------

#ifndef STATIC_LIB_HH
#define STATIC_LIB_HH

namespace vigil
{

// xxx This is busted on big-endian, right?
//-----------------------------------------------------------------------------
template <uint16_t in>
struct htons_
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
    static const uint16_t val = (in<<8&0xff00)|(in>>8&0x00ff);
#elif __BYTE_ORDER == __BIG_ENDIAN
    static const uint16_t val = in;
#else
#error " BYTE ORDERING not specified "
#endif
};
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
template <uint32_t in>
struct htonl_
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
    static const uint32_t val =
        (in<<24&0xff000000)|
        (in<<8 &0x00ff0000)|
        (in>>8 &0x0000ff00)|
        (in>>24&0x000000ff);
#elif __BYTE_ORDER == __BIG_ENDIAN
    static const uint32_t val = in;
#else
#error " BYTE ORDERING not specified "
#endif
};
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
template <uint64_t in>
struct htonll_
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
    static const uint64_t val =
        (in<<56&0xff00000000000000ULL)|
        (in<<40&0x00ff000000000000ULL)|
        (in<<24&0x0000ff0000000000ULL)|
        (in<<8 &0x000000ff00000000ULL)|
        (in>>8 &0x00000000ff000000ULL)|
        (in>>24&0x0000000000ff0000ULL)|
        (in>>40&0x000000000000ff00ULL)|
        (in>>56&0x00000000000000ffULL);
#elif __BYTE_ORDER == __BIG_ENDIAN
    static const uint64_t val = in;
#else
#error " BYTE ORDERING not specified "
#endif
};
//-----------------------------------------------------------------------------

}

#endif  // -- STATIC_LIB_HH
