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
#ifndef FNV_HASH_HH
#define FNV_HASH_HH 1

#include <inttypes.h>

namespace vigil
{

/* Calculates and returns the Fowler-Noll-Vo hash of the 'size' bytes in
 * 'data', starting with the given initial hash 'value'.  (The default 'value'
 * is the recommended FNV offset basis.) */
inline uint32_t fnv_hash(const void* data, size_t size,
                         uint32_t value = UINT32_C(2166136261))
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    while (size-- > 0)
    {
        value *= 0x01000193;    /* Magic Fowler-Noll-Vo prime for 32-bit. */
        value ^= *p++;
    }
    return value;
}

} // namespace vigil

#endif /* fnv_hash.hh */
