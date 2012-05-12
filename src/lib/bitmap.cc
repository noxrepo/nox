/* Copyright 2009 (C) Nicira, Inc.
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

#include "bitmap.hh"

namespace vigil
{

static const uint64_t b = 1;
const uint64_t Bitmap::PART_VALUES[] =
{
    b << 0,  b << 1,  b << 2,  b << 3,  b << 4,
    b << 5,  b << 6,  b << 7,  b << 8,  b << 9,
    b << 10, b << 11, b << 12, b << 13, b << 14,
    b << 15, b << 16, b << 17, b << 18, b << 19,
    b << 20, b << 21, b << 22, b << 23, b << 24,
    b << 25, b << 26, b << 27, b << 28, b << 29,
    b << 30, b << 31, b << 32, b << 33, b << 34,
    b << 35, b << 36, b << 37, b << 38, b << 39,
    b << 40, b << 41, b << 42, b << 43, b << 44,
    b << 45, b << 46, b << 47, b << 48, b << 49,
    b << 50, b << 51, b << 52, b << 53, b << 54,
    b << 55, b << 56, b << 57, b << 58, b << 59,
    b << 60, b << 61, b << 62, b << 63
};

} /* namespace vigil */

