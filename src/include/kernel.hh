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
#ifndef KERNEL_HH
#define KERNEL_HH 1

#include <ctime>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include <boost/function.hpp>
#include <boost/utility.hpp>

#include "component.hh"
#include "errors.hh"
#include "hash_map.hh"
#include "hash_set.hh"

namespace vigil
{

class Deployer;

typedef std::list<Component_context*> Component_context_list;
typedef std::list<Deployer*> Deployer_list;
typedef hash_map<Component_name, Component_context*>
Component_name_context_map;
typedef std::list<Component_context*> Component_context_list;
typedef hash_map<int, Component_context_list*>
Component_state_context_set_map;

/* NOX kernel is a dependency management engine.  It does nothing else
   but maintains the inter-component dependencies and takes care of
   installing any necessary components other components depend on.

   TODO: make the kernel monitor...
*/
class Kernel : boost::noncopyable
{
public:
    /* Time in seconds since boot. */
    time_t uptime() const;

    /* Get the universally unique identifier of this NOX instance. */
    typedef uint64_t UUID;
    UUID uuid() const;

    /* How many times NOX container has been rebooted since its
       installation. */
    uint64_t restarts() const;

    /* Attach a deployer to be used if an unknown component is asked
       to be installed. */
    void attach_deployer(Deployer*);

    /* Get deployers */
    Deployer_list get_deployers() const;

    /* Retrieve a component context. Return 0 if no such component
       context known. Note this guarantees nothing about the state of
       the given component. */
    Component_context* get(const Component_name&) const;

    /* Retrieve a component context. Return 0 if no such component
       context not known or not in the required state.*/
    Component_context* get(const Component_name&,
                           const Component_state) const;

    /* Retrieve all component contexts, regardless of their state. */
    Component_context_list get_all() const;

    /* Install a component by delegating the installation process to
       attached deployers. Throws an exception if no component can be
       found. */
    void install(const Component_name&, const Component_state)
    throw(state_change_error);

    /* Install a component to a given state. */
    void install(Component_context*, const Component_state to_state);

    /* Change the state of a component. Throws an exception if a fatal
       error occurs during the state transition(s). */
    void change(Component_context*, const Component_state to_state)
    throw(state_change_error);

    void set_argument(const Component_name&, const Component_argument&);

    const Component_argument get_argument(const Component_name&) const;

    /* Get global kernel singleton.  Note, this is *not* for general
       use, but for unit test framework integration and NOX platform
       internal use only. */
    static Kernel* get_instance();

    /* Initialize the global kernel singleton.  Note, this is for the
       NOX platform to use only. */
    static void init(const std::string&, int argc, char** argv);

    /* Keep argc and argv around (primarily tro pass to python */
    int argc;
    char** argv;

private:
    /* Currently, kernel is a singleton and . */
    Kernel(const std::string&, int argc, char** argv);

    /* Attempt to resolve dependencies. */
    void resolve();

    /* Attempt to resolve dependencies from a state to next one. */
    bool resolve(const Component_state from, const Component_state to);

    /* Attempt to resolve dependencies of a given set of contexts to
       next state. */
    Component_context_list resolve(const Component_context_list&,
                                   const Component_state to);

    /* Deployers to try if a new component context needs to be
       created. */
    Deployer_list deployers;

    /* Every context */
    Component_name_context_map contexts;

    /* Per state context */
    Component_state_context_set_map per_state;

    /* Application properties */
    typedef hash_map<Component_name, Component_argument>
    Component_argument_map;
    Component_argument_map arguments;

    /* Set if a context required state has been changed while
       resolving. */
    bool state_requirements_changed;

    /* Set if the kernel is installing a component context right
       now. */
    bool installing;

    /* start time. used for uptime */
    time_t start_time;

    /* Persistent container information unique to this container instance.
     * Retrieved and modified from/in a persistent storage in the
     * beginning of the boot sequence.  If nothing exists on the storage,
     * the container creates a new info block with random UUID. */
    struct container_info
    {
        /* Universally Unique Identifier (UUID).  Not 128-bits as defined
         * in the RFC 4122, but 64-bits for the convenience. */
        uint64_t uuid;

        /* The number of times the container has been restarted. */
        uint64_t restart_counter;
    };
    container_info info;
};

} // namespace vigil

#endif
