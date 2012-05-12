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
#include "dso-deployer.hh"

#include <dlfcn.h>

#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "fault.hh"
#include "vlog.hh"

using namespace std;
using namespace vigil;

static Vlog_module lg("dso-deployer");

static lt_dlhandle
open_library(const char* library, const char** error_msg)
{
	lt_dlhandle h = 0;
	lt_dladvise advise;
	if (!lt_dladvise_init(&advise)
			&& !lt_dladvise_ext(&advise)
			&& !lt_dladvise_global(&advise))
		h = lt_dlopenadvise(library, advise);
	lt_dladvise_destroy (&advise);
    //lt_dlhandle h = lt_dlopenext(library);
    *error_msg = h ? "" : lt_dlerror();
    return h;
}

DSO_deployer::DSO_deployer(const Component_context* ctxt,
                           const list<string>& lib_dirs)
    : Component(ctxt), lib_dirs(lib_dirs)
{
    namespace fs = boost::filesystem;
    namespace pt = boost::property_tree;

    /* Initialize preloaded symbol table */
    LTDL_SET_PRELOADED_SYMBOLS();

    /* Initialize the libtool dynamic library loading facilities */
    if (lt_dlinit())
    {
        throw runtime_error("lt_dlinit() failed: " +
                            demangle_undefined_symbol(lt_dlerror()));
    }

    if (!lt_dlopen(0))
    {
        throw runtime_error("lt_dlopen() for the main() failed: " +
                            demangle_undefined_symbol(lt_dlerror()));
    }

    list<fs::path> description_files;
    BOOST_FOREACH (string directory, lib_dirs)
    {
        list<fs::path> results = scan(directory);
        description_files.insert(description_files.end(),
                                 results.begin(), results.end());
    }

    BOOST_FOREACH (fs::path& config_path, description_files)
    {
        try
        {
            // read the config file into config_root
            pt::ptree root;
            pt::read_json(config_path.string(), root);

            BOOST_FOREACH (const pt::ptree::value_type& component, root)
            {
                const std::string& name = component.first;
                Component_context* ctxt =
                    new DSO_component_context(name, config_path.string());
                if (uninstalled_contexts.find(ctxt->get_name()) ==
                        uninstalled_contexts.end())
                {
                    uninstalled_contexts[ctxt->get_name()] = ctxt;
                    VLOG_DBG(lg, "Component '%s' scanned.", ctxt->get_name().c_str());
                }
                else
                {
                    VLOG_ERR(lg, "Component '%s' declared multiple times.",
                             ctxt->get_name().c_str());
                    delete ctxt;
                }
            }
        }
        catch (const bad_cast& e)
        {
            // Not a DSO component, skip.
            continue;
        }
        catch (const pt::ptree_error& e)
        {
            VLOG_ERR(lg, "Cannot parse '%s': %s", config_path.string().c_str(),
                     e.what());
        }
    }

    Kernel* kernel = Kernel::get_instance();

    /* Cross-check they are no duplicate component definitions across
       deployers. */
    BOOST_FOREACH(const Deployer* deployer, kernel->get_deployers())
    {
        BOOST_FOREACH(Component_context* ctxt, deployer->get_contexts())
        {
            Component_name_context_map::iterator i =
                uninstalled_contexts.find(ctxt->get_name());
            if (i != uninstalled_contexts.end())
            {
                VLOG_ERR(lg, "Component '%s' declared multiple times.",
                         ctxt->get_name().c_str());
                uninstalled_contexts.erase(i);
            }
        }
    }

    /* Finally, register itself as a deployer responsible for DSO
       components. */
    kernel->attach_deployer(this);
}

DSO_deployer::~DSO_deployer()
{

}

Component*
DSO_deployer::instantiate(const Component_context* ctxt,
                          const Path_list& lib_search_paths)
{
    return new DSO_deployer(ctxt, lib_search_paths);
}

void
DSO_deployer::configure()
{
}

void
DSO_deployer::install()
{
}

DSO_deployer::Path_list
DSO_deployer::get_search_paths() const
{
    return lib_dirs;
}

DSO_component_context::DSO_component_context(const Component_name& name,
        const std::string& config_path)
    : Component_context(name, config_path)
{
    using namespace boost;

    install_actions[DESCRIBED] = bind(&DSO_component_context::describe, this);
    install_actions[LOADED] = bind(&DSO_component_context::load, this);
    install_actions[FACTORY_INSTANTIATED] =
        bind(&DSO_component_context::instantiate_factory, this);
    install_actions[INSTANTIATED] =
        bind(&DSO_component_context::instantiate, this);
    install_actions[CONFIGURED] = bind(&DSO_component_context::configure, this);
    install_actions[INSTALLED] =
        bind(&DSO_component_context::install, this);

    library = properties.get<std::string>("library");

    if (has("dependencies"))
    {
        BOOST_FOREACH (const std::string& dependency,
                       get_config_list("dependencies"))
        {
            dependencies.push_back(new Name_dependency(dependency));
        }
    }
}

void
DSO_component_context::describe()
{
    /* Dependencies were introduced in the constructor */
    current_state = DESCRIBED;
}

void
DSO_component_context::load()
{
    namespace fs = boost::filesystem;
    fs::path home_path(properties.get<std::string>("home_path"));
    const char* error_msg;

    handle = open_library((home_path / library).string().c_str(), &error_msg);
    string error(demangle_undefined_symbol(error_msg));

    /* A little extra check for libtool build directory */
    if (!handle)
    {
        handle = open_library(
                     (home_path / ".libs" / library).string().c_str(),
                     &error_msg);
        error = "'" + error + "' or '"+demangle_undefined_symbol(error_msg)+"'";
    }

    if (!handle)
    {
        current_state = ERROR;
        error_message = "Cannot open a dynamic library: " + error;
    }
    else
    {
        current_state = LOADED;
    }
}

DSO_component_context::component_factory_function*
DSO_component_context::find_factory_function(const char* name) const
{
    component_factory_function* f =
        reinterpret_cast<component_factory_function*>(lt_dlsym(handle, name));
    return f;
}

static const string replace(const string& s, const char c, const string& n)
{
    string v = s;
    while (true)
    {
        string::size_type p = v.find(c, 0);
        if (p == string::npos)
        {
            return v;
        }

        v = v.replace(p, 1, n);
    }
}

void
DSO_component_context::instantiate_factory()
{
    component_factory_function* f = 0;

    /* Prefer a factory function with the embedded component name, but if
       that's not found, default to one without the name. */
    string function_with_component_name =
        replace(library, '-', "_") + "_get_factory";

    f = find_factory_function(function_with_component_name.c_str());
    if (!f)
    {
        f = find_factory_function("get_factory");
    }
    if (!f)
    {
        current_state = ERROR;
        error_message = library + " does not implement " +
                        function_with_component_name + "() nor get_factory() function";
        return;
    }

    factory = f();
    interface = factory->get_interface();
    current_state = FACTORY_INSTANTIATED;
}

void
DSO_component_context::instantiate()
{
    try
    {
        component = factory->instance(this);
        current_state = INSTANTIATED;
    }
    catch (const std::exception& e)
    {
        error_message = e.what();
        current_state = ERROR;
    }
}

void
DSO_component_context::configure()
{
    try
    {
        component->configure();
        current_state = CONFIGURED;
    }
    catch (const std::exception& e)
    {
        error_message = e.what();
        current_state = ERROR;
    }
}

void
DSO_component_context::install()
{
    try
    {
        component->install();
        current_state = INSTALLED;
    }
    catch (const std::exception& e)
    {
        error_message = e.what();
        current_state = ERROR;
    }
}

