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
#ifndef STRING_HH
#define STRING_HH 1

#include <stdarg.h>
#include <string>

namespace vigil
{

void string_printf(std::string&, const char* format, ...)
__attribute__((format(printf, 2, 3)));
void string_vprintf(std::string&, const char* format, va_list args)
__attribute__((format(printf, 2, 0)));
std::string string_format(const char* format, ...)
__attribute__((format(printf, 1, 2)));

} // namespace vigil

#endif /* string.hh */
