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
#ifndef DATAPATHID_HH
#define DATAPATHID_HH

#include <inttypes.h>
#include <string>
#include <stdio.h>
#include <boost/functional/hash.hpp>

#include "xtoxll.h"

namespace vigil
{

/* Represents the 48-bit datapath ID that uniquely identifies a NOX switch. */
class datapathid
{
public:
    datapathid();
    datapathid(const datapathid&);
    static datapathid from_host(uint64_t host_order);
    static datapathid from_net(uint64_t net_order);
    static datapathid from_bytes(const uint8_t bytes[6]);

    uint64_t as_host() const;
    uint64_t as_net() const;

    bool operator==(const datapathid&) const;
    bool operator!=(const datapathid&) const;
    bool operator<(const datapathid&) const;
    bool operator<=(const datapathid&) const;
    bool operator>(const datapathid&) const;
    bool operator>=(const datapathid&) const;
    bool empty() const;

    std::string string() const;
private:
    uint64_t id;                /* In host byte order. */

    datapathid(uint64_t id_) : id(id_) { }
};

inline
datapathid::datapathid()
    : id(0)
{}

inline
datapathid::datapathid(const datapathid& that)
    : id(that.id)
{}

inline datapathid
datapathid::from_host(uint64_t host_order)
{
    return datapathid(host_order);
}

inline datapathid
datapathid::from_net(uint64_t net_order)
{
    return datapathid(ntohll(net_order));
}

inline datapathid
datapathid::from_bytes(const uint8_t bytes[6])
{
    uint64_t net = 0;
    memcpy((char*)&net + 2, bytes, 6);
    return from_net(net);
}


inline uint64_t
datapathid::as_host() const
{
    return id;
}

inline uint64_t
datapathid::as_net() const
{
    return htonll(id);
}

inline bool
datapathid::operator==(const datapathid& that) const
{
    return id == that.id;
}

inline bool
datapathid::operator!=(const datapathid& that) const
{
    return id != that.id;
}

inline bool
datapathid::operator<(const datapathid& that) const
{
    return id < that.id;
}

inline bool
datapathid::operator<=(const datapathid& that) const
{
    return id <= that.id;
}

inline bool
datapathid::operator>(const datapathid& that) const
{
    return id > that.id;
}

inline bool
datapathid::operator>=(const datapathid& that) const
{
    return id >= that.id;
}

inline bool
datapathid::empty() const
{
    return id == 0;
}

inline std::string
datapathid::string() const
{
    char buf[24];
    sprintf(buf, "%012" PRIx64, as_host());
    return buf;
}

inline std::size_t hash_value(const datapathid& dpid)
{
    boost::hash<uint64_t> hasher;
    return hasher(dpid.as_host());
}

struct datapathid_hash 
{
    std::size_t operator()(const datapathid& dpid) const
    {
        boost::hash<uint64_t> hasher;
        return hasher(dpid.as_host());
    }
};

} // namespace vigil

#endif  // -- DATAPATHID_HH
