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

#ifndef IPADDR_PORT_HH
#define IPADDR_PORT_HH 1

#include "ipaddr.hh"
#include "fnv_hash.hh"

namespace vigil
{

struct ipaddr_port
{
    ipaddr addr;
    uint16_t port;

    ipaddr_port();
    ipaddr_port(const std::string&, const uint16_t);

    std::string string() const;
    operator std::string () const;

    bool operator == (const ipaddr_port&) const;
} __attribute__ ((__packed__)); // -- struct ipaddr_port

inline
ipaddr_port::ipaddr_port() : addr(), port(0) { }

inline
ipaddr_port::ipaddr_port(const std::string& addr_in, const uint16_t port_in)
    : addr(addr_in), port(port_in) { }

inline
ipaddr_port::operator std::string() const
{
    char buf[6];
#ifndef NDEBUG
    const int ret =
#endif
        ::snprintf(buf, sizeof(buf), "%"PRIu16"", port);
    assert(ret < 6);
    return addr.string() + ':' + buf;
}

inline
std::string
ipaddr_port::string() const
{
    return operator std::string();
}

inline
bool
ipaddr_port::operator == (const ipaddr_port& in) const
{
    return addr == in.addr && port == in.port;
}

} // namespace vigil

/* G++ 4.2+ has hash template specializations in std::tr1, while
   versions before expect the specilizations to exist in
   __gnu_cxx. Hence the namespace macro magic. */
ENTER_HASH_NAMESPACE

template<>
struct hash<vigil::ipaddr_port>
        : public std::unary_function<vigil::ipaddr_port, std::size_t>
{
    std::size_t
    operator()(const vigil::ipaddr_port& iap) const
    {
        const uint32_t ip = iap.addr;
        const uint32_t value = vigil::fnv_hash(&ip, sizeof(ip));
        return vigil::fnv_hash(&iap.port, sizeof(iap.port), value);
    }
};

EXIT_HASH_NAMESPACE

#endif // -- IPADDR_PORT_HH
