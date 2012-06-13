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
#ifndef STATIC_DEPLOYER_HH
#define STATIC_DEPLOYER_HH 1

#include <memory>

#include <boost/filesystem.hpp>
#include <boost/function.hpp>

#include "component.hh"
#include "kernel.hh"

namespace vigil
{

/*
 * A component context for components that required no binary loading.
 *
 * The context supports both components providing a factory as well as
 * callback based component construction.
 */
class Static_component_context
    : public Component_context
{
public:
    typedef boost::function<Component* (Component_context*)>
    Constructor_callback;

    Static_component_context(const Constructor_callback&,
                             const Interface_description&,
                             const Component_name& name,
                             const std::string& config_path);

    Static_component_context(const Component_factory*,
                             const Component_name& name,
                             const std::string& config_path);

private:
    void init_actions(const Interface_description&);

    /* Actions implementing state transitions */
    void describe();
    void load();
    void instantiate_factory();
    void instantiate();
    void configure();
    void install();

    /* Constructor or factory instance to use to an instantiate a
       component. */
    const Constructor_callback constructor;
    const Component_factory* factory;
};

}

#endif
