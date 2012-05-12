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
#include "netinet++/ethernetaddr.hh"
#include "string.hh"

using namespace vigil;

static int
hexit_value(int c)
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

void
ethernetaddr::init_from_string(const char* str)
{
    uint8_t new_octet[LEN];

    for (int i = 0; i < 6; i++)
    {
        int digit1 = hexit_value(*str++);
        if (digit1 < 0)
        {
            goto error;
        }
        new_octet[i] = digit1;

        int digit2 = hexit_value(*str);
        if (digit2 >= 0)
        {
            new_octet[i] = new_octet[i] * 16 + digit2;
            ++str;
        }

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

std::string
ethernetaddr::string() const
{
    return vigil::string_format("%02x:%02x:%02x:%02x:%02x:%02x",
                                octet[0], octet[1], octet[2],
                                octet[3], octet[4], octet[5]);
}
