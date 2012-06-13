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
#ifndef HASH_SET_HH
#define HASH_SET_HH 1

#include "hash.hh"

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ > 1)
#include <tr1/unordered_set>

namespace vigil
{
template < class Value,
         class Hash = std::tr1::hash<Value>,
         class Equal = std::equal_to<Value>,
         class Allocator = std::allocator<Value> >
class hash_set
    : public std::tr1::unordered_set<Value, Hash, Equal, Allocator>
{
};
} // namespace vigil
#else /* G++ before 4.2 */
#include "hash_func.hh"
#include <ext/hash_set>
#include <stdint.h>

namespace vigil
{
template < class Value,
         class Hash = __gnu_cxx::hash<Value>,
         class Equal = std::equal_to<Value>,
         class Allocator = std::allocator<Value> >
class hash_set
    : public __gnu_cxx::hash_set<Value, Hash, Equal, Allocator>
{
};
} // namespace vigil

#endif /* G++ before 4.2 */

#endif /* hash-set.hh */
