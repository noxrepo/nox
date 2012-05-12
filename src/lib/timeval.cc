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
#include "timeval.hh"
#include <boost/static_assert.hpp>
#include <cassert>
#include <cstdlib>
#include "type-props.h"
#include "vlog.hh"

#define NOT_TESTED() ((void) 0) /* XXX should print a message. */

namespace vigil
{
static Vlog_module lg("timeval");
}

/* POSIX allows floating-point time_t, but we don't support it. */
BOOST_STATIC_ASSERT(TYPE_IS_INTEGER(time_t));

/* We do try to cater to unsigned time_t, but I want to know about it if we
 * ever encounter such a platform. */
BOOST_STATIC_ASSERT(TYPE_IS_SIGNED(time_t));

/* Returns the current time as a count of milliseconds since the epoch. */
long long int
time_msec()
{
    ::timeval tv = do_gettimeofday();
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

::timeval do_gettimeofday(const bool update)
{
    static ::timeval tv;
    if (update)
    {
        int error = gettimeofday(&tv, NULL);
        if (error < 0)
        {
            abort();
        }
    }
    return tv;
}

const ::timeval&
operator+=(::timeval& x, const ::timeval& y)
{
    x.tv_sec += y.tv_sec;
    x.tv_usec += y.tv_usec;
    if (x.tv_usec >= 1000000)
    {
        ++x.tv_sec;
        x.tv_usec -= 1000000;
    }
    return x;
}

::timeval
operator+(const ::timeval& x, const ::timeval& y)
{
    ::timeval sum = x;
    sum += y;
    return sum;
}

const ::timeval&
operator-=(::timeval& x, const ::timeval& y)
{
    /* A time_t, and hence a timeval, cannot necessarily hold a negative
     * value, so for compatibility when time goes backward (NTP, etc.)
     * we set a floor of zero for the subtraction. */
    if (x <= y)
    {
        vigil::lg.warn("Returning 0 in place of negative timeval");
        x.tv_sec = 0;
        x.tv_usec = 0;
    }
    else
    {
        assert(x >= y);
        if (x.tv_usec >= y.tv_usec)
        {
            x.tv_usec -= y.tv_usec;
            x.tv_sec -= y.tv_sec;
        }
        else
        {
            x.tv_usec = 1000000 + x.tv_usec - y.tv_usec;
            x.tv_sec = x.tv_sec - y.tv_sec - 1;
        }
    }
    return x;
}

::timeval
operator-(const ::timeval& x, const ::timeval& y)
{
    ::timeval difference = x;
    difference -= y;
    return difference;
}

int timeval_compare(const ::timeval& x, const ::timeval& y)
{
    if (x.tv_sec != y.tv_sec)
    {
        return x.tv_sec < y.tv_sec ? -1 : 1;
    }
    else if (x.tv_usec != y.tv_usec)
    {
        return x.tv_usec < y.tv_usec ? -1 : 1;
    }
    else
    {
        return 0;
    }
}

long int timeval_to_ms(const ::timeval& x)
{
    return (x.tv_sec >= LONG_MAX / 1000 - 999 ? LONG_MAX
            : x.tv_sec <= LONG_MIN / 1000 + 999 ? LONG_MIN
            : x.tv_sec >= 0 ? x.tv_sec * 1000 + (x.tv_usec + 500) / 1000
            : x.tv_sec * 1000 - (x.tv_usec + 500) / 1000);
}

::timeval timeval_from_ms(long int ms)
{
    if (ms >= 0)
    {
        return (ms / 1000 < TYPE_MAXIMUM(time_t)
                ? make_timeval(ms / 1000, ms % 1000 * 1000)
                : make_timeval(TYPE_MAXIMUM(time_t), 0));
    }
    else
    {
        NOT_TESTED();           /* Rounding here seems suspicious. */
        return (ms / 1000 > TYPE_MINIMUM(time_t)
                ? make_timeval(ms / 1000, -(ms % 1000) * 1000)
                : make_timeval(TYPE_MINIMUM(time_t), 0));
    }
}


long int timespec_to_ms(const ::timespec& x)
{
    return (x.tv_sec >= LONG_MAX / 1000 - 999 ? LONG_MAX
            : x.tv_sec <= LONG_MIN / 1000 + 999 ? LONG_MIN
            : x.tv_nsec >= 0 ? x.tv_sec * 1000 + (x.tv_nsec + 500000) / 1000000
            : x.tv_sec * 1000 + (x.tv_nsec - 500000) / 1000000);
}

::timespec timespec_from_ms(long int ms)
{
    if (ms >= 0)
    {
        return (ms / 1000 < TYPE_MAXIMUM(time_t)
                ? make_timespec(ms / 1000, ms % 1000 * 1000000)
                : make_timespec(TYPE_MAXIMUM(time_t), 0));
    }
    else
    {
        NOT_TESTED();           /* Rounding here seems suspicious. */
        ms -= 999;
        return (ms / 1000 > TYPE_MINIMUM(time_t)
                ? make_timespec(ms / 1000, -(ms % 1000) * 1000000)
                : make_timespec(TYPE_MINIMUM(time_t), 0));
    }
}

double timeval_to_double(const ::timeval& x)
{
    return x.tv_sec + x.tv_usec / 1000000.0;
}

double timespec_to_double(const ::timespec& x)
{
    return x.tv_sec + x.tv_nsec / 1000000000.0;
}
