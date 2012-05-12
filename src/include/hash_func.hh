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
#ifndef HASH_FUNC_HH
#define HASH_FUNC_HH

#include <string>

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ > 1)
#include <tr1/functional>
#else /* G++ before 4.2 */
#include <ext/hash_fun.h>

namespace __gnu_cxx
{

template <class T>
struct hash<T*>
{
    size_t operator()(const T* t) const
    {
        return (uintptr_t) t;
    }
};

template <>
struct hash<std::string>
{
    size_t operator()(const std::string& s) const
    {
        return hash<const char*>()(s.c_str());
    }
};

} // namespace __gnu_cxx
#endif /* G++ before 4.2 */

#endif /* hash_func.hh */
