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
#ifndef ASSERT_HH
#define ASSERT_HH 1

/*
 * assert_cast<T>(x) is equivalent to static_cast<T>(x), except that for safety
 * it asserts that the similar dynamic_cast<T>(x) is valid.
 */

#include <cassert>

namespace vigil
{

/* Cast to reference. */
template <class T, class R>
inline T assert_cast(R& x)
{
    assert(&dynamic_cast<T>(x) == &x);
    return static_cast<T>(x);
}

/* Cast to pointer. */
template <class T, class R>
inline T assert_cast(R* x)
{
    assert(dynamic_cast<T>(x) == x);
    return static_cast<T>(x);
}

} // namespace vigil

#endif /* assert.hh */
