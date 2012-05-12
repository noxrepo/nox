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
#include "command-line.hh"
#include <exception>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include "vlog.hh"

namespace vigil
{

std::string long_options_to_short_options(const struct option* options)
{
    std::string short_options;
    for (; options->name; options++)
    {
        const struct option* o = options;
        if (o->flag == NULL && o->val > 0 && o->val <= UCHAR_MAX)
        {
            short_options.push_back(o->val);
            if (o->has_arg == required_argument)
            {
                short_options.push_back(':');
            }
            else if (o->has_arg == optional_argument)
            {
                short_options.append("::");
            }
        }
    }
    return short_options;
}

// null arg: Set everything to dbg mode
//
// otherwise: set up to specification in config string.  If
//            parsing fails, default to dbg mode for all
//            modules
#ifndef LOG4CXX_ENABLED
void set_verbosity(const char* arg)
{
    if (!arg)
    {
        vlog().set_levels(Vlog::FACILITY_CONSOLE, Vlog::ANY_MODULE,
                          Vlog::LEVEL_DBG);
    }
    else
    {
        std::string ret;
        ret = vlog().set_levels_from_string(arg);
        if (ret != "ack")
        {
            fprintf(stderr, "error parsing log string: %s\n", ret.c_str());
            exit(EXIT_FAILURE);
        }
    }
}
#endif

} // namespace vigil
