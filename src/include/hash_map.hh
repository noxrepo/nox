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
#ifndef HASH_MAP_HH
#define HASH_MAP_HH 1

#include "hash.hh"

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ > 1)
#include <tr1/unordered_map>
#include <utility>

namespace vigil
{
template < class Key,
         class Tp,
         class Hash = std::tr1::hash<Key>,
         class Equal = std::equal_to<Key>,
         class Allocator = std::allocator<std::pair<const Key, Tp> > >
class hash_map
    : public std::tr1::unordered_map<Key, Tp, Hash, Equal, Allocator>
{
};
} // namespace vigil
#else /* G++ before 4.2 */
#include "hash_func.hh"
#include <ext/hash_map>
#include <stdint.h>

namespace vigil
{
template < class Key,
         class Tp,
         class Hash = __gnu_cxx::hash<Key>,
         class Equal = std::equal_to<Key>,
         class Allocator = std::allocator<Key> >
class hash_map
    : public __gnu_cxx::hash_map<Key, Tp, Hash, Equal, Allocator>
{
};
} // namespace vigil
#endif /* G++ before 4.2 */

#endif /* hash-map.hh */
