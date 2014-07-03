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
// Yet another ip address class
//
//-----------------------------------------------------------------------------

#ifndef IPADDR_HH
#define IPADDR_HH

#include <string>
#include <cstdio>
#include <cassert>
#include <cstring>
#include <typeinfo>
#include <iostream>
#include <stdexcept>
#include <netdb.h>
#include <inttypes.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <boost/functional/hash.hpp>

namespace vigil
{

//-----------------------------------------------------------------------------
//                  bad_ipaddr_cast
//-----------------------------------------------------------------------------
class bad_ipaddr_cast : public std::bad_cast
{

public:
    bad_ipaddr_cast() :
        source(&typeid(void)), target(&typeid(void))
    {
    }
    bad_ipaddr_cast(
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
        return "bad ip address cast: "
               "source string value could not be interpreted as ip address";
    }
    virtual ~bad_ipaddr_cast() throw()
    {
    }
private:
    const std::type_info* source;
    const std::type_info* target;
};


struct ipaddr
{
    uint32_t addr;

    // ------------------------------------------------------------------------
    // Constructors
    // ------------------------------------------------------------------------

    ipaddr();
    ipaddr(uint32_t);
    ipaddr(const ipaddr&);
    explicit ipaddr(const char*);
    ipaddr(const in_addr&);
    ipaddr(const uint8_t*);
    ipaddr(const sockaddr&);
    explicit ipaddr(const std::string&);

    // ------------------------------------------------------------------------
    // String Representation
    // ------------------------------------------------------------------------

    void        fill_string(char* in) const;
    void        fill_string(std::string& in) const;
    std::string string() const;

    // ------------------------------------------------------------------------
    // Casting Operators
    // ------------------------------------------------------------------------

    operator bool () const;
    operator uint32_t () const;
    operator std::string() const;

    // ------------------------------------------------------------------------
    // Binary Operators
    // ------------------------------------------------------------------------

    bool    operator !() const;
    ipaddr  operator ~() const;
    ipaddr  operator & (const ipaddr&) const;
    ipaddr  operator & (uint32_t) const;
    ipaddr& operator &= (const ipaddr&);
    ipaddr& operator &= (uint32_t);
    ipaddr  operator | (const ipaddr&) const;
    ipaddr  operator | (uint32_t) const;
    ipaddr& operator |= (const ipaddr&);
    ipaddr& operator |= (uint32_t);

    // ------------------------------------------------------------------------
    // Mathematical operators
    // ------------------------------------------------------------------------

    ipaddr operator ++ ();
    ipaddr operator ++ (int);
    ipaddr operator -- ();
    ipaddr operator += (int);
    ipaddr operator + (int) const;

    int    operator - (const ipaddr&) const;
    ipaddr operator - (int) const;

    // ------------------------------------------------------------------------
    // Assignment
    // ------------------------------------------------------------------------

    ipaddr& operator = (const ipaddr&);
    ipaddr& operator = (uint32_t);

    // ------------------------------------------------------------------------
    // Comparison Operators
    // ------------------------------------------------------------------------

    bool operator == (const ipaddr&) const;
    bool operator == (uint32_t) const;
    bool operator != (const ipaddr&) const;
    bool operator != (uint32_t) const;
    bool operator < (uint32_t) const;
    bool operator < (const ipaddr&) const;
    bool operator <= (uint32_t) const;
    bool operator <= (const ipaddr&) const;
    bool operator > (uint32_t) const;
    bool operator > (const ipaddr&) const;
    bool operator >= (uint32_t) const;
    bool operator >= (const ipaddr&) const;

    // ------------------------------------------------------------------------
    // Convenient functions
    // ------------------------------------------------------------------------

    /** Indicate address is multicast
     */
    bool isMulticast();
    /** Indicate address is private IP space
     */
    bool isPrivate();

} __attribute__((__packed__));  // -- struct ipaddr

//-----------------------------------------------------------------------------
inline
ipaddr::ipaddr()
{
    addr = 0;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr::ipaddr(const char* addr_in)
{
    struct hostent* hp = 0;

    // -- REQUIRES
    assert(addr_in);

    if ((hp = ::gethostbyname(addr_in)) == 0)
    {
        // -- Quick hack b/c I don't know how to get swig and C++
        //    exceptions to cooperate
        addr = 0;
        throw bad_ipaddr_cast();
    }

    // -- CHECK
    assert(hp->h_length == 4);

    memcpy(&addr, hp->h_addr, hp->h_length);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr::ipaddr(const ipaddr& in)
{
    addr = in.addr;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr::ipaddr(const std::string& addr_in)
{
    struct hostent* hp = 0;

    if ((hp = ::gethostbyname(addr_in.c_str())) == 0)
    {
        throw bad_ipaddr_cast();
    }

    // -- CHECK
    assert(hp->h_length == 4);

    memcpy(&addr, hp->h_addr, hp->h_length);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr::ipaddr(const sockaddr& addr_in)
{
    if (addr_in.sa_family != AF_INET)
    {
        throw bad_ipaddr_cast();
    }

    addr = *((uint32_t*)&addr_in);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr::ipaddr(const in_addr& addr_in)
{
    addr = *((uint32_t*)&addr_in);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr::ipaddr(const uint8_t* addr_in)
{
    // -- REQUIRES
    assert(addr_in);

    addr = *((uint32_t*)addr_in);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr::ipaddr(uint32_t addr_in)
{
    addr = addr_in;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
void
ipaddr::fill_string(std::string& in) const
{
    in = this->string();
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
void
ipaddr::fill_string(char* in) const
{
    // -- REQUIRES
    assert(in);

    std::string ret;
    ret = this->string();

    ::strncpy(in, ret.c_str(), INET_ADDRSTRLEN);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
std::string
ipaddr::string() const
{
    char  buf[INET_ADDRSTRLEN];

    if (! ::inet_ntop(AF_INET, ((struct in_addr*)&addr), buf, INET_ADDRSTRLEN))
    {
        return "0.0.0.0";
    }

    return std::string(buf);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr::operator std::string() const
{
    char* tmp = 0;

    if ((tmp = ::inet_ntoa(*((struct in_addr*)&addr))) == 0)
    {
        return "0.0.0.0";
    }

    return std::string(tmp);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr::operator bool () const
{
    return !(addr == 0);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr::operator uint32_t () const
{
    return addr;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr
ipaddr::operator ~() const
{
    return ipaddr(htonl(~addr));
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr
ipaddr::operator & (const ipaddr& in) const
{
    return ipaddr(htonl(addr & in.addr));
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr
ipaddr::operator & (uint32_t in) const
{
    return ipaddr(ntohl(addr) & in);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr&
ipaddr::operator &= (const ipaddr& in)
{
    addr &= in.addr;
    return *this;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr&
ipaddr::operator &= (uint32_t in)
{
    addr &= in;
    return *this;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr
ipaddr::operator | (const ipaddr& in) const
{
    return ipaddr(htonl(addr | in.addr));
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr
ipaddr::operator | (uint32_t in) const
{
    return ipaddr(htonl(addr | in));
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr&
ipaddr::operator |= (const ipaddr& in)
{
    addr |= in.addr;
    return *this;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr&
ipaddr::operator |= (uint32_t in)
{
    addr |= in;
    return *this;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr&
ipaddr::operator = (const ipaddr& in)
{
    addr = in.addr;
    return *this;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr&
ipaddr::operator = (uint32_t in)
{
    addr = in;
    return *this;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool
ipaddr::operator == (const ipaddr& in) const
{
    return addr == in.addr;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool
ipaddr::operator == (uint32_t in) const
{
    return addr == in;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool
ipaddr::operator != (const ipaddr& in) const
{
    return addr != in.addr;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool
ipaddr::operator != (uint32_t in) const
{
    return addr != in;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr
ipaddr::operator ++ ()
{
    addr = htonl(htonl(addr) + 1);
    return *this;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr
ipaddr::operator ++ (int)
{
    ipaddr ret = *this;
    ++(*this);
    return ret;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr
ipaddr::operator -- ()
{
    addr = htonl(htonl(addr) - 1);
    return *this;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr
ipaddr::operator += (int in)
{
    addr = htonl(htonl(addr) + in);
    return *this;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr
ipaddr::operator + (int in) const
{
    return ipaddr(htonl(addr) + in);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
int
ipaddr::operator - (const ipaddr& in) const
{
    return htonl(addr) - htonl(in.addr);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
ipaddr
ipaddr::operator - (int in) const
{
    return ipaddr(htonl(addr) - in);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool
ipaddr::operator < (uint32_t in) const
{
    return htonl(addr) < in;
}

//-----------------------------------------------------------------------------
inline
bool
ipaddr::operator < (const ipaddr& in) const
{
    return htonl(addr) < htonl(in.addr);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool
ipaddr::operator <= (uint32_t in) const
{
    return htonl(addr) <= in;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool
ipaddr::operator <= (const ipaddr& in) const
{
    return htonl(addr) <= htonl(in.addr);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool
ipaddr::operator > (uint32_t in) const
{
    return htonl(addr) > in;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool
ipaddr::operator > (const ipaddr& in) const
{
    return htonl(addr) > htonl(in.addr);
}
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
inline
bool
ipaddr::operator >= (uint32_t in) const
{
    return htonl(addr) >= in;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool
ipaddr::operator !() const
{
    return addr == 0x00000000;
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool
ipaddr::operator >= (const ipaddr& in) const
{
    return htonl(addr) >= htonl(in.addr);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
std::ostream&
operator <<(std::ostream& os, const ipaddr& addr)
{
    os << addr.string();
    return os;
}
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
inline
bool ipaddr::isMulticast()
{
    return (ntohl(addr) >= 0xE0000000L) && (ntohl(addr) <= 0xEFFFFFFFL);
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
inline
bool ipaddr::isPrivate()
{
    return ((ntohl(addr) >= 0x10000000L) && (ntohl(addr) <= 0x10FFFFFFL)) || \
           ((ntohl(addr) >= 0xAC100000L) && (ntohl(addr) <= 0xAC1FFFFFL)) || \
           ((ntohl(addr) >= 0xC0A80000L) && (ntohl(addr) <= 0xC0ABFFFFL));
}
//-----------------------------------------------------------------------------

inline std::size_t hash_value(const ipaddr& ip)
{
    boost::hash<uint64_t> hasher;
    return hasher(ip.addr);
};

struct ipaddr_hash
{
    std::size_t operator()(const ipaddr& ip) const
    {
        boost::hash<uint64_t> hasher;
        return hasher(ip.addr);
    }
};

}

#endif  // -- IPADDR_HH
