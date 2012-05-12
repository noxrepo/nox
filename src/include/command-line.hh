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
/* Utilities for command-line parsing. */

#ifndef COMMAND_LINE_HH
#define COMMAND_LINE_HH 1

#include <string>
#include <memory>

#include "config.h"

struct option;

namespace vigil
{

std::string long_options_to_short_options(const struct option* options);
#ifndef LOG4CXX_ENABLED
void set_verbosity(const char* arg);
#endif

} // namespace vigil

#endif /* command-line.hh */
