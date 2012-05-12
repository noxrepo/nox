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
#ifndef DSO_DEPLOYER_HH
#define DSO_DEPLOYER_HH 1

#include "config.h"

#include <ltdl.h>

#include <list>
#include <vector>

#include "component.hh"
#include "deployer.hh"
#include "kernel.hh"

namespace vigil
{

/*
 * A dynamic shared object deployer.
 *
 * While constructed, the DSO deployer scans the directory structure
 * for any components being implemented as DSOs.
 */
class DSO_deployer
    : public Component, public Deployer
{
public:
    virtual ~DSO_deployer();

    typedef std::list<std::string> Path_list;

    static Component* instantiate(const Component_context*,
                                  const Path_list& lib_search_paths);

    void configure();
    void install();

    /* Return the DSO search paths */
    Path_list get_search_paths() const;

private:
    DSO_deployer(const Component_context* ctxt,
                 const Path_list& lib_search_paths);

    const Path_list lib_dirs;
};

class DSO_component_context
    : public Component_context
{
public:
    DSO_component_context(const Component_name& name,
                          const std::string& config_path);

private:
    /* Actions implementing state transitions */
    void describe();
    void load();
    void instantiate_factory();
    void instantiate();
    void configure();
    void install();

    typedef Component_factory* component_factory_function();
    component_factory_function* find_factory_function(const char*) const;

    /* Library name */
    std::string library;

    /* Handle to DSO */
    ::lt_dlhandle handle;

    /* Factory instance */
    Component_factory* factory;
};

}

#endif
