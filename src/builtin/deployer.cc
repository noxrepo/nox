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
#include "deployer.hh"

#include <sstream>
#include <boost/foreach.hpp>

using namespace vigil;

Deployer::~Deployer()
{
}

bool
Deployer::deploy(Kernel* kernel, const Component_name& name)
{
    Component_name_context_map::iterator i = uninstalled_contexts.find(name);

    if (i == uninstalled_contexts.end())
    {
        return false;
    }

    Component_context* ctxt = i->second;
    uninstalled_contexts.erase(i);

    kernel->install(ctxt, NOT_INSTALLED);
    return true;
}

std::string Deployer::CONFIG_FILE = "meta.json";

Deployer::Path_list
Deployer::scan(boost::filesystem::path p)
{
    ;
    using namespace boost::filesystem;

    Path_list description_files;

    if (!exists(p))
    {
        return description_files;
    }

    directory_iterator end;
    for (directory_iterator j(p); j != end; ++j)
    {
        try
        {
            if (!is_directory(j->status()) &&
                j->path().leaf() == CONFIG_FILE)
            {
                description_files.push_back(j->path());
                continue;
            }

            if (is_directory(j->status()))
            {
                Path_list result = scan(*j);
                description_files.insert(description_files.end(),
                                         result.begin(), result.end());
            }
        }
        catch (...)
        {
            /* Ignore any directory browsing errors. */
        }
    }

    return description_files;
}

Component_context_list
Deployer::get_contexts() const
{
    Component_context_list l;

    for (Component_name_context_map::const_iterator i =
             uninstalled_contexts.begin();
         i != uninstalled_contexts.end(); ++i)
    {
        l.push_back(i->second);
    }

    return l;
}
