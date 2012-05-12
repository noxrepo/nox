/* Copyright 2008, 2009 (C) Nicira, Inc.
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
#ifndef DEPLOYER_HH
#define DEPLOYER_HH 1

#include <list>
#include <string>

#include <boost/filesystem.hpp>

#include "component.hh"
#include "hash_map.hh"
#include "kernel.hh"

namespace vigil
{

/* A deployer constructs a context and passes it for the kernel when
 * asked for.  NOX supports multiple deployers, and indeed, NOX has a
 * deployer for statically linked components, dynamically linked
 * components as well for components implemented in Python.
 */
class Deployer
{
public:
    virtual ~Deployer();
    virtual bool deploy(Kernel*, const Component_name&);

    static std::string CONFIG_FILE;

    /* Find recursively any JSON component description files. */
    typedef std::list<boost::filesystem::path> Path_list;
    static Path_list scan(boost::filesystem::path);

    /* Get all contexts the deployer knows about. */
    Component_context_list get_contexts() const;

protected:
    /* Components known about, but not installed into the kernel. */
    Component_name_context_map uninstalled_contexts;
};
}

#endif
