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
#include "string.hh"

#include <stdlib.h>
#include <stdio.h>

#define NOT_REACHED() abort()

namespace vigil
{

void
string_vprintf(std::string& string, const char* format, va_list args)
{
    char buffer[128];

    va_list args2;
    va_copy(args2, args);
    int n = vsnprintf(buffer, sizeof buffer, format, args2);
    va_end(args2);

    if (n >= sizeof buffer)
    {
        string.resize(string.size() + n);
        va_copy(args2, args);
        vsnprintf(&string[string.size()], n + 1, format, args2);
        va_end(args2);
    }
    else if (n >= 0)
    {
        string += buffer;
    }
    else
    {
        NOT_REACHED();
    }
}

void
string_printf(std::string& string, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    string_vprintf(string, format, args);
    va_end(args);
}

std::string
string_format(const char* format, ...)
{
    std::string string;

    va_list args;
    va_start(args, format);
    string_vprintf(string, format, args);
    va_end(args);

    return string;
}

} // namespace vigil
