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
//----------------------------------------------------------------------------
// Description:
//
// Encapsulate and ethernet address so we can convert, copy.. etc.
//
//     +-----+-----+---------------------------+
//     | I/G | U/L |     46 bits address       |
//     +-----+-----+---------------------------+
//
//     I/G bit indicates whether the MAC address is of unicast or
//     of multicast.
//
//     U/L bit indicates whether the MAC address is universal or private.
//     The rest of 46 bits field is used for frame  filtering.
//
//     References:
//     http://cell-relay.indiana.edu/mhonarc/mpls/1997-Mar/msg00031.html
//     http://www.iana.org/assignments/ethernet-numbers
//
//-----------------------------------------------------------------------------

#ifndef ETHERNETADDR_HH
#define ETHERNETADDR_HH

#include "config.h"
#include <stdexcept>
#include <ostream>
#include <cassert>
#include <typeinfo>
#include <cstring>
#include <string>
#include <netinet/in.h>
#include <stdint.h>
#include "string.hh"

#include <boost/serialization/binary_object.hpp>
#include <boost/serialization/is_bitwise_serializable.hpp>
#include <boost/functional/hash.hpp>

#include "xtoxll.h"

namespace
{

//-----------------------------------------------------------------------------
//                             hexit_value helper routine
//-----------------------------------------------------------------------------
int
hexit_value2(int c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    else if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 0xa;
    }
    else if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 0xa;
    }
    else
    {
        return -1;
    }
}
//-----------------------------------------------------------------------------

} // namespace

namespace vigil
{

//-----------------------------------------------------------------------------
//                  bad_ethernetaddr_cast
//-----------------------------------------------------------------------------
class bad_ethernetaddr_cast : public std::bad_cast
{

public:
    bad_ethernetaddr_cast() :
        source(&typeid(void)), target(&typeid(void))
    {
    }
    bad_ethernetaddr_cast(
        const std::type_info& source_type,
        const std::type_info& target_type) :
        source(&source_type), target(&target_type)
    {
    }
    const std::type_info& source_type() const
    {
        return *source;
    }
    const std::type_info& target_type() const
    {
        return *target;
    }
    virtual const char* what() const throw()
    {
        return "bad ethernet address cast: "
               "source string value could not be interpreted as ethernet address";
    }
    virtual ~bad_ethernetaddr_cast() throw()
    {
    }
private:
    const std::type_info* source;
    const std::type_info* target;
};

//-----------------------------------------------------------------------------
//                             struct ethernetaddr
//-----------------------------------------------------------------------------

static const uint8_t ethbroadcast[] = "\xff\xff\xff\xff\xff\xff";
static const uint8_t pae_multicast[] = "\x01\x80\xc2\x00\x00\x03";

/* printf formatting for ethernetaddr. */
#define EA_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#define EA_ARGS(ea)                                     \
    (ea)->octet[0], (ea)->octet[1], (ea)->octet[2], \
    (ea)->octet[3], (ea)->octet[4], (ea)->octet[5]
//-----------------------------------------------------------------------------
struct ethernetaddr
{
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    static const  unsigned int   LEN         =   6;


    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    uint8_t     octet[ethernetaddr::LEN];

    //-------------------------------------------------------------------------
    // Constructors/Destructor
    //-------------------------------------------------------------------------
    ethernetaddr();
    ethernetaddr(const char*);
    ethernetaddr(const unsigned char[LEN]);
    ethernetaddr(uint64_t id);
    ethernetaddr(const std::string&);
    ethernetaddr(const ethernetaddr&);

    // ------------------------------------------------------------------------
    // String Representation
    // ------------------------------------------------------------------------

    std::string string() const;

    uint64_t    hb_long() const;
    uint64_t    nb_long() const;

    //-------------------------------------------------------------------------
    // Overloaded casting operator
    //-------------------------------------------------------------------------
    operator const uint8_t* () const;
    operator const uint16_t* () const;
    operator const struct ethernetaddr* () const;

    //-------------------------------------------------------------------------
    // Overloaded assignment operator
    //-------------------------------------------------------------------------
    ethernetaddr& operator=(const ethernetaddr&  octet);
    ethernetaddr& operator=(const char*          text);
    ethernetaddr& operator=(uint64_t               id);

    // ------------------------------------------------------------------------
    // Comparison Operators
    // ------------------------------------------------------------------------

    bool operator == (const ethernetaddr&) const;
    bool operator != (const ethernetaddr&) const;
    bool operator < (const ethernetaddr&) const;
    bool operator <= (const ethernetaddr&) const;
    bool operator > (const ethernetaddr&) const;
    bool operator >= (const ethernetaddr&) const;

    //-------------------------------------------------------------------------
    // Non-Const Member Methods
    //-------------------------------------------------------------------------

    void set_octet(const uint8_t* oct);

    //-------------------------------------------------------------------------
    // Method: private(..)
    //
    // Check whether the private bit is set
    //-------------------------------------------------------------------------
    bool is_private() const;

    bool is_init() const;

    //-------------------------------------------------------------------------
    // Method: is_multicast(..)
    //
    // Check whether the multicast bit is set
    //-------------------------------------------------------------------------
    bool is_multicast() const;

    bool is_broadcast() const;

    bool is_pae() const;

    bool is_zero() const;

    template<class Archive> void serialize(Archive&, unsigned int);

private:
    void init_from_string(const char*);
} __attribute__((__packed__));
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ethernetaddr::ethernetaddr()
{
    memset(octet, 0, LEN);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ethernetaddr::ethernetaddr(const ethernetaddr& addr_in)
{
    ::memcpy(octet, addr_in.octet, LEN);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ethernetaddr::ethernetaddr(uint64_t id)
{
    if ((id & 0xffff000000000000ULL) != 0)
    {
        // TODO
        //std::cerr << " ethernetaddr::operator=(uint64_t) warning, value "
        //      << "larger then 48 bits, truncating" << std::endl;
    }

    id = htonll(id);

#if __BYTE_ORDER == __BIG_ENDIAN
    ::memcpy(octet, &id, LEN);
#else
    ::memcpy(octet, ((uint8_t*)&id) + 2, LEN);
#endif
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ethernetaddr::ethernetaddr(const char* text)
{
    init_from_string(text);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ethernetaddr::ethernetaddr(const unsigned char octet_[LEN])
{
    ::memcpy(octet, octet_, LEN);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ethernetaddr::ethernetaddr(const std::string& text)
{
    init_from_string(text.c_str());
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
std::string
ethernetaddr::string() const
{
    return vigil::string_format("%02x:%02x:%02x:%02x:%02x:%02x",
                                octet[0], octet[1], octet[2],
                                octet[3], octet[4], octet[5]);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ethernetaddr&
ethernetaddr::operator=(const ethernetaddr& addr_in)
{
    ::memcpy(octet, addr_in.octet, LEN);
    return *this;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ethernetaddr&
ethernetaddr::operator=(uint64_t               id)
{
    if ((id & 0xffff000000000000ULL) != 0)
    {
        // TODO
        //std::cerr << " ethernetaddr::operator=(uint64_t) warning, value "
        //            << "larger then 48 bits, truncating" << std::endl;
    }
    ::memcpy(octet, &id, LEN);
    return *this;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool
ethernetaddr::is_init() const
{
    return
        (*((uint32_t*)octet) != 0) &&
        (*(((uint16_t*)octet) + 2) != 0) ;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ethernetaddr&
ethernetaddr::operator=(const char* addr_in)
{
    init_from_string(addr_in);
    return *this;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool
ethernetaddr::operator==(const ethernetaddr& addr_in) const
{
    for (unsigned int i = 0 ; i < LEN ; i++)
    {
        if (octet[i] != addr_in.octet[i])
        {
            return false;
        }
    }
    return true;
}
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
inline
bool
ethernetaddr::operator!=(const ethernetaddr& addr_in) const
{
    for (unsigned int i = 0; i < LEN; i++)
        if (octet[i] != addr_in.octet[i])
            return true;
    return false;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool
ethernetaddr::operator < (const ethernetaddr& in) const
{
    return ::memcmp(octet, in.octet, LEN) < 0;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool
ethernetaddr::operator <= (const ethernetaddr& in) const
{
    return ::memcmp(octet, in.octet, LEN) <= 0;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool
ethernetaddr::operator > (const ethernetaddr& in) const
{
    return ::memcmp(octet, in.octet, LEN) > 0;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool
ethernetaddr::operator >= (const ethernetaddr& in) const
{
    return ::memcmp(octet, in.octet, LEN) >= 0;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
void
ethernetaddr::set_octet(const uint8_t* octet_in)
{
    ::memcpy(octet, octet_in, LEN);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ethernetaddr::operator const struct ethernetaddr* () const
{
    return reinterpret_cast<const ethernetaddr*>(octet);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ethernetaddr::operator const uint8_t* () const
{
    return reinterpret_cast<const uint8_t*>(octet);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ethernetaddr::operator const uint16_t* () const
{
    return reinterpret_cast<const uint16_t*>(octet);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
uint64_t
ethernetaddr::hb_long() const
{
    uint64_t id = *((uint64_t*)octet);
    return (ntohll(id)) >> 16;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
uint64_t
ethernetaddr::nb_long() const
{
    uint64_t id = *((uint64_t*)octet);
#if __BYTE_ORDER == __BIG_ENDIAN
    return (id >> 16);
#else
    return (id & 0xffffffffffffULL);
#endif
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool ethernetaddr::is_private() const
{
    return((0x02 & octet[0]) != 0);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool ethernetaddr::is_multicast() const
{
    return((0x01 & octet[0]) != 0);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool ethernetaddr::is_broadcast() const
{
    return (hb_long() == 0xffffffffffffULL);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool ethernetaddr::is_pae() const
{
    return (hb_long() == 0x0180c2000003ULL);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool
ethernetaddr::is_zero() const
{
    return ((*(uint32_t*)octet) == 0) && ((*(uint16_t*)(octet + 4)) == 0);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
template <class Archive>
inline
void
ethernetaddr::serialize(Archive& ar, const unsigned int)
{
    ar& boost::serialization::make_binary_object(octet, sizeof octet);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
void
ethernetaddr::init_from_string(const char* str)
{
    uint8_t new_octet[LEN];

    for (int i = 0; i < 6; i++)
    {
        int digit1 = hexit_value2(*str++);
        if (digit1 < 0)
        {
            goto error;
        }
        new_octet[i] = digit1;

        int digit2 = hexit_value2(*str);
        if (digit2 >= 0)
        {
            new_octet[i] = new_octet[i] * 16 + digit2;
            ++str;
        }
        else
            goto error;

        if (i != 5 && *str != '-' && *str != ':')
        {
            goto error;
        }
        if (i < 5)
        {
            ++str;
        }
    }
    if (*str)
    {
        goto error;
    }
    memcpy(octet, new_octet, LEN);
    return;

error:
    throw bad_ethernetaddr_cast();
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
std::ostream&
operator <<(std::ostream& os, const ethernetaddr& addr_in)
{
    os << addr_in.string();
    return os;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline std::size_t hash_value(const ethernetaddr& ethaddr)
{
    return boost::hash_range(ethaddr.octet, ethaddr.octet + sizeof ethaddr.octet);
}
//-----------------------------------------------------------------------------

struct ethernetaddr_hash
{
    std::size_t operator()(const ethernetaddr& ethaddr) const
    {
        return boost::hash_range(ethaddr.octet, ethaddr.octet + sizeof ethaddr.octet);
    }
};


} // namespace vigil

BOOST_IS_BITWISE_SERIALIZABLE(vigil::ethernetaddr);

#endif   // ETHERNETADDR_HH
