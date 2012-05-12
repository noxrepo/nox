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
#include "static-deployer.hh"

#include <iostream>

#include <boost/bind.hpp>
#include <boost/exception/exception.hpp>
#include <boost/exception/info.hpp>
#include <boost/exception/error_info.hpp>
#include <boost/exception/get_error_info.hpp>
#include <boost/system/error_code.hpp>

#include "deployer.hh"

using namespace std;
using namespace vigil;

typedef boost::error_info<struct tag_errmsg, std::string> errmsg_info;

Static_component_context::
Static_component_context(const Constructor_callback& constructor_,
                         const Interface_description& interface,
                         const Component_name& name,
                         const std::string& config_path)
    : Component_context(name, config_path),
      constructor(constructor_), factory(0)
{
    init_actions(interface);
}

Static_component_context::
Static_component_context(const Component_factory* f,
                         const Component_name& name,
                         const std::string& config_path)
    : Component_context(name, config_path),
      constructor(0), factory(f)
{
    init_actions(f->get_interface());
}

void
Static_component_context::init_actions(
    const Interface_description& interface)
{
    using namespace boost;

    install_actions[DESCRIBED] =
        bind(&Static_component_context::describe, this);
    install_actions[LOADED] =
        bind(&Static_component_context::load, this);
    install_actions[FACTORY_INSTANTIATED] =
        bind(&Static_component_context::instantiate_factory, this);
    install_actions[INSTANTIATED] =
        bind(&Static_component_context::instantiate, this);
    install_actions[CONFIGURED] =
        bind(&Static_component_context::configure, this);
    install_actions[INSTALLED] =
        bind(&Static_component_context::install, this);

    this->interface = interface;
}

void
Static_component_context::describe()
{
    /* Dependencies were introduced in the constructor */
    current_state = DESCRIBED;
}

void
Static_component_context::load()
{
    /* Statically linked components don't require loading */
    current_state = LOADED;
}

void
Static_component_context::instantiate_factory()
{
    /* Statically linked components don't require factory instantiating */
    current_state = FACTORY_INSTANTIATED;
}

void
Static_component_context::instantiate()
{
    try
    {
        component = factory ? factory->instance(this) : constructor(this);
        current_state = INSTANTIATED;
    }
    catch (const std::exception& e)
    {
        const std::string* err = boost::get_error_info<errmsg_info>(e);
        error_message = err ? *err : e.what();
        current_state = ERROR;
    }
}

void
Static_component_context::configure()
{
    try
    {
        component->configure();
        current_state = CONFIGURED;
    }
    catch (const std::exception& e)
    {
        const std::string* err = boost::get_error_info<errmsg_info>(e);
        error_message = err ? *err : e.what();
        current_state = ERROR;
    }
}

void
Static_component_context::install()
{
    try
    {
        component->install();
        current_state = INSTALLED;
    }
    catch (const std::exception& e)
    {
        const std::string* err = boost::get_error_info<errmsg_info>(e);
        error_message = err ? *err : e.what();
        current_state = ERROR;
    }
}
