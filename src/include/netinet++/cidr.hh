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

#ifndef CIDR_IPADDR_HH
#define CIDR_IPADDR_HH

#include "ipaddr.hh"

namespace vigil
{

struct cidr_ipaddr
{
    ipaddr addr;
    uint32_t mask;

    cidr_ipaddr() : addr(), mask(~((uint32_t)0)) {}
    cidr_ipaddr(const ipaddr&, uint8_t mask_bit_len);
    explicit cidr_ipaddr(const char*);
    explicit cidr_ipaddr(const std::string&);

    bool matches(const ipaddr&) const;

    uint32_t get_prefix_len() const;

    void fill_string(std::string& in) const;
    std::string string() const;

    bool operator == (const cidr_ipaddr&) const;
    bool operator != (const cidr_ipaddr&) const;

private:
    void set_mask(const char* slash);

}; // -- struct cidr_ipaddr

inline
cidr_ipaddr::cidr_ipaddr(const ipaddr& ip, uint8_t mask_bit_len)
    : addr(ip)
{
    if (mask_bit_len > 32)
    {
        throw bad_ipaddr_cast();
    }
    else if (mask_bit_len == 0)
    {
        mask = 0;
    }
    else
    {
        mask = htonl((~((uint32_t)0)) << (32 - mask_bit_len));
    }
    addr.addr = addr.addr & mask;
}

inline
void
cidr_ipaddr::set_mask(const char* slash)
{
    const char* start = slash + 1;
    while (*start != '\0')
    {
        if (!isdigit(*(start++)))
        {
            throw bad_ipaddr_cast();
        }
    }
    if (start == slash + 1)
    {
        throw bad_ipaddr_cast();
    }
    int len = atoi(slash + 1);
    if (len > 32)
    {
        throw bad_ipaddr_cast();
    }
    else if (len == 0)
    {
        mask = 0;
    }
    else
    {
        mask = htonl((~((uint32_t)0)) << (32 - len));
    }
}

inline
cidr_ipaddr::cidr_ipaddr(const char* cidr_str)
{
    const char* slash = strchr(cidr_str, '/');

    if (slash)
    {
        addr = ipaddr(std::string(cidr_str, slash - cidr_str));
        set_mask(slash);
    }
    else
    {
        addr = ipaddr(cidr_str);
        mask = ~((uint32_t)0);
    }
    addr.addr = addr.addr & mask;
}

inline
cidr_ipaddr::cidr_ipaddr(const std::string& cidr_str)
{
    size_t idx = cidr_str.find('/');
    if (idx != std::string::npos)
    {
        addr = ipaddr(cidr_str.substr(0, idx));
        set_mask(cidr_str.c_str() + idx);
    }
    else
    {
        addr = ipaddr(cidr_str);
        mask = ~((uint32_t)0);
    }
    addr.addr = addr.addr & mask;
}

inline
bool
cidr_ipaddr::matches(const ipaddr& ip) const
{
    return addr.addr == (ip.addr & mask);
}

inline
uint32_t
cidr_ipaddr::get_prefix_len() const
{
    uint32_t len = 0;
    uint32_t mask_cpy = ntohl(mask);
    while (mask_cpy != 0)
    {
        mask_cpy = mask_cpy << 1;
        ++len;
    }
    return len;
}

inline
void
cidr_ipaddr::fill_string(std::string& in) const
{
    char buf[4];
#ifndef NDEBUG
    int ret =
#endif
        snprintf(buf, 4, "%"PRIu32"", get_prefix_len());
    assert(ret < 4);
    in = addr.string() + '/' + buf;
}


inline
std::string
cidr_ipaddr::string() const
{
    std::string str;
    fill_string(str);
    return str;
}

inline
bool
cidr_ipaddr::operator == (const cidr_ipaddr& in) const
{
    return addr == in.addr && mask == in.mask;
}

inline
bool
cidr_ipaddr::operator != (const cidr_ipaddr& in) const
{
    return addr != in.addr || mask != in.mask;
}


}

#endif  // -- CIDR_IPADDR_HH
