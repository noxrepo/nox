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
// ------------------------------------------------------------
// Container API
//
// ------------------------------------------------------------

#ifndef PUBLIC_CONTAINER_HH
#define PUBLIC_CONTAINER_HH 1

#include <list>
#include <vector>
#include <string>
#include <typeinfo>
#include <utility>

#include <boost/asio.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/function.hpp>
#include <boost/noncopyable.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/shared_array.hpp>

#include "event.hh"
#include "hash_map.hh"
#include "hash_set.hh"

namespace vigil
{

class Event_dispatcher;
class Dependency;
class Kernel;

/* Valid component states. */
enum Component_state
{
    NOT_INSTALLED = 0,        /* Component has been discovered. */
    DESCRIBED = 1,            /* Component description has been processed. */
    LOADED = 2,               /* Component library has been loaded, if
                                 necessary. */
    FACTORY_INSTANTIATED = 3, /* Component factory has been
                                 instantiated from the library. */
    INSTANTIATED = 4,         /* Component itself has been instantiated. */
    CONFIGURED = 5,           /* Component's configure() has been called. */
    INSTALLED = 6,            /* Component's install() has been called. */
    ERROR = 7                 /* Any state transition can lead to error state,
                                 if an unrecovarable error is occurs. */
};

const std::string Component_state_string[] =
{
    "NOT_INSTALLED",
    "DESCRIBED",
    "LOADED",
    "FACTORY_INSTANTIATED",
    "INSTANTIATED",
    "CONFIGURED",
    "INSTALLED",
    "ERROR"
};

typedef std::string Interface_description;
typedef std::string Component_name;
typedef std::string Component_argument;

class Component;

/* Components can not interact through static variables.  For
 * accessing the container and to discover other components, container
 * passes a context instance for them.
 *
 * Kernel does not directly operate on components, but on component
 * contexts, which contain all per component information - including
 * the component instance itself.
 * The object contains both parsed
 * configuration and any command line arguments defined for the
 * component by the user.
 */
class Component_context
{
public:
    Component_context(const Component_name& name,
                      const std::string& config_path);
    virtual ~Component_context();

    /* State transition management methods */
    virtual void install(const Component_state to_state);
    virtual void uninstall(const Component_state to_state);

    /* Attempts to resolve the dependencies for the given state.
       Returns true if all the dependencies are met. */
    bool resolve_dependencies(const Component_state);

    /* Accessors for member variables */
    Interface_description get_interface() const;
    Component* get_instance() const;
    Component_state get_state() const;
    Component_state get_required_state() const;
    void set_required_state(const Component_state);

    /* Return a human-readable status report. */
    std::string get_status() const;
    std::string get_error_message(hash_set<Component_context*>) const;

    /* Public Component_context methods to be provided for components. */
    Component_name get_name() const;
    Component* resolve_by_name(const Component_name&) const;
    Component* get_by_name(const Component_name&) const;
    Component* get_by_interface(
        const Interface_description&) const;
    Kernel* get_kernel() const;

    // CONFIGURATION
    /* Return the value of a configuration key. */
    template<class Type> Type get_config(const std::string& key) const
    {
        return properties.get<Type>(key);
    }

    /* Return the value of a configuration key. */
    template<class Type> Type
    get_config(const std::string& key, const Type& default_value) const
    {
        return properties.get(key, default_value);
    }

    const boost::property_tree::ptree&
    get_config_child(const std::string& key) const;

    /* Return the value of a configuration key. */
    std::list<std::string> get_config_list(const std::string& key) const;

    /* Return the ptree under a configuration key. */
    const boost::property_tree::ptree&
    get_property_tree(const std::string& key) const;

    /* Return true if the key exists. */
    const bool has(const std::string&) const;

protected:
    /* Human readable component name */
    Component_name name;

    /* Interface identifier */
    Interface_description interface;

    /* configuration translated into a property tree */
    boost::property_tree::ptree properties;

    /* Current and required component states */
    Component_state current_state;
    Component_state required_state;
    std::string error_message;

    /* If has been instantianted, the actual component instance */
    Component* component;

    /* Any component dependencies */
    typedef std::list<Dependency*> Dependency_list;
    Dependency_list dependencies;

    /* Action implementations */
    typedef boost::function<void()> Action_callback;
    hash_map<int, Action_callback> install_actions;

    /* Kernel hosting the context */
    Kernel* kernel;
};

/** \defgroup noxcomponents NOX Components
 *
 * A Component encapsulates specific functionality made available to NOX.
 *
 * NOX applications are generally componsed of cooperating components
 * that provide the required functionality.
 */

/** \ingroup noxcomponents
 *
 * Base class for all components.
 *
 * Component see the component state transitions as instantiation and
 * method calls by the container:
 *
 * - NOT_INSTALLED           - state of every component in the beginning.
 * - DESCRIBED               - component description has been parsed.
 * - LOADED                  - dynamic library loaded (if any).
 * - FACTORY_INSTANTIATED    - component factory constructed
 * - INSTANTIATED            - component constructed.
 * - CONFIGURED              - component configure()'d.
 * - INSTALLED               - component install()'ed.
 * - ERROR
 */
class Component
    : boost::noncopyable, public boost::enable_shared_from_this<Component>
{
public:
    typedef Disposition Event_handler_signature(const Event&);
    typedef boost::function<Event_handler_signature> Event_handler;

    Component(const Component_context*);
    virtual ~Component();

    /**
     * Configure the component.
     *
     * The method should block until the configuration completes.
     */
    virtual void configure() {}

    /**
     * Start the component.
     *
     * The method should block until the component runs.  Container
     * will not pass any method invocations before starting completes.
     */
    virtual void install() {}

    /* Event management methods */
    /* Register an event */
    template <typename T>
    inline
    void register_event() const
    {
        register_event(T::static_get_name());
    }

    /* Not preferred mechanism for registering an event. */
    void register_event(const Event_name&) const;

    /* Register an event handler */
    template <typename T>
    inline
    void register_handler(const Event_handler& h) const
    {
        register_handler(T::static_get_name(), h);
    }

    /* Register an event handler */
    void register_handler(const Event_name&, const Event_handler&) const;

    /* Dispatch an event directly */
    void dispatch(const Event&) const;

    /* Post an event */
    void post(const Event&) const;
    
    /* Posts 'callback' to be called after the given 'duration' elapses.  The
     * caller must not destroy the returned Timer, but may use it to cancel or
     * reschedule the timer, up until the point where the timer is actually
     * invoked. */
    typedef boost::function<void(const boost::system::error_code& error)>Callback;
    std::unique_ptr<boost::asio::deadline_timer>
    post_callback(const Callback& callback, const timeval& duration);

protected:
    /* Component_context to access the container */
    const Component_context* ctxt;

    /* A handle to event_dispatcher */
    Event_dispatcher* event_dispatcher;
};

/* Applications provide a component factory for the container.
 *
 * While loading the component in, the container asks for a factory
 * instance by calling get_factory() and then constructs (and destroys)
 * all the component instances using the factory.
 */
class Component_factory
{
public:
    Component_factory();
    virtual ~Component_factory();
    virtual Component* instance(const Component_context*) const = 0;
    virtual Interface_description get_interface() const = 0;
    virtual void destroy(Component*) const = 0;
};

/* Basic component factory template for simple application needs. */
template <typename T>
class Simple_component_factory
    : public Component_factory
{
public:
    Simple_component_factory(const Interface_description& i_) : i(i_) { }
    Simple_component_factory() : i(typeid(T).name()) { }

    Component* instance(const Component_context* c) const
    {
        return new T(c);
    }

    Interface_description get_interface() const
    {
        return i;
    }

    void destroy(Component*) const
    {
        throw std::runtime_error("not implemented");
    }

private:
    const Interface_description i;
};

/* An abstract dependency. */
class Dependency
{
public:
    virtual ~Dependency();

    /* Attempts to resolve the dependency, and executes any necessary
       actions. Returns true if the dependency has been met.*/
    virtual bool resolve(const Component_state to_state) = 0;

    /* Get a human readable status report. */
    virtual std::string get_status() const = 0;

    /* Get a human readable error message about the dependency failure
       (if any). */
    virtual std::string get_error_message(hash_set<Component_context*>)
    const = 0;
};

/* A basic name dependency is met only when the given component has
   been installed. */
class Name_dependency
    : public Dependency
{
public:
    Name_dependency(const Component_name&);
    bool resolve(const Component_state);
    std::string get_status() const;
    std::string get_error_message(hash_set<Component_context*>) const;

private:
    const Component_name name;
};


/* If the compiler is not provided with a factory function name, use empty
   component name. */
#ifndef __COMPONENT_FACTORY_FUNCTION__
#define __COMPONENT_FACTORY_FUNCTION__ get_factory
#endif

/* Register components implemented as dynamic libraries */
#define REGISTER_COMPONENT(COMPONENT_FACTORY, COMPONENT_CLASS)  \
    extern "C"                                                  \
    vigil::Component_factory*                                   \
    __COMPONENT_FACTORY_FUNCTION__() {                          \
        static vigil::Interface_description                     \
        i(typeid(COMPONENT_CLASS).name());                      \
        static COMPONENT_FACTORY f(i);                          \
        return &f;                                              \
    }

} // namespace vigil

#endif
