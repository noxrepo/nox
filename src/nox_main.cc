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

/** \mainpage
 *
 * \section overview Overview
 *
 * This automatically generated documentation for NOX is currently a
 * work in progress.  Documentation of APIs is very hit and miss.  The
 * code is therefore the best reference for API details.  This focus
 * of the doxygen documentation effort at this time is the areas in
 * which newcomers to NOX have the most questions:
 *
 * - The \ref nox-programming-model "asynchronous, event-based programming model"
 * - The existing \ref noxcomponents "components providing application functionality"
 * - The \ref noxevents "events core NOX and the components generate"
 * - Access to current information on network principles through the bindings-storage component
 * - Saving of persistent data using the storage component
 *
 */

/** \page nox-programming-model The NOX Programming Model
 *
 * ... Write a nice intro here ...
 *
 * \section prog-model-threading Multi-threaded 
 *
 * \section prog-model-async Asynchronous Programming
 * \subsection prog-model-async-c C++
 *
 * \section prog-model-events Event Programming
 * \subsection prog-model-events-using Using Existing Events
 * \subsection prog-model-events-creating Creating New Events
 *
 */


#include "config.h"

#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <inttypes.h>
#include <unistd.h>

#include <list>
#include <set>
#include <string>

#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/function.hpp>
#include <boost/foreach.hpp>
#include <boost/lambda/lambda.hpp>
#include <boost/thread.hpp>

#include "bootstrap-complete-event.hh"
#include "event-dispatcher.hh"
#include "command-line.hh"
#include "component.hh"
#include "connection-manager.hh"
#include "dso-deployer.hh"
#include "event.hh"
#include "fault.hh"
#include "kernel.hh"
#include "shutdown-event.hh"
#include "static-deployer.hh"
#include "vlog.hh"
#ifndef LOG4CXX_ENABLED
#include "vlog-socket.hh"
#else
#include "log4cxx/helpers/exception.h"
#include "log4cxx/xml/domconfigurator.h"
#endif

using namespace vigil;
using namespace std;

// TODO: MOVE SOME FUNCTIONALITY TO KERNEL

namespace
{

Vlog_module lg("main");

typedef std::string Application_name;
typedef std::pair<Application_name, std::string> Application_param;
typedef std::list<Application_param> Application_list;

Application_list
parse_application_list(int optind, int argc, char** argv)
{
    Application_list applications;

    set<string> applications_defined;
    for (int i = optind; i < argc; i++)
    {
        string str(argv[i]);
        string app_name(str), app_arg("");
        size_t index_equal = str.find('=');
        size_t index = str.find(':');
        // separate into (comp_name,arg) by : or = dependeing on which occurs first
        if (index_equal < index)
            index = index_equal;
        // the component doesn't have any args
        if (index != string::npos)
        {
            app_name = str.substr(0, index);
            if (index + 1 < str.length())
            {
                app_arg = str.substr(index + 1, str.length() - index - 1);
            }
        }

        if (applications_defined.find(app_name) != applications_defined.end())
        {
            VLOG_ERR(lg, "Application '%s' defined multiple times in the command line. "
                     "Ignoring the latter definition(s).", app_name.c_str());
            continue;
        }
        applications_defined.insert(app_name);
        applications.push_back(make_pair(app_name, app_arg));
    }

    return applications;
}

void hello(const char* program_name)
{
    printf("NOX %s (%s), compiled " __DATE__ " " __TIME__ "\n", NOX_VERSION,
           program_name);
}

// TODO: move unreliable option to the configuration
void usage(const char* program_name)
{
    printf("%s: nox runtime\n"
           "usage: %s [OPTIONS] [APP[=ARG[,ARG]...]] [APP[=ARG[,ARG]...]]...\n"
           "\nInterface options (specify any number):\n"
           "  -i ptcp:[IP]:[PORT]     listen to TCP PORT on interface specified by IP\n"
           "                          (default: 0.0.0.0:%d)\n"
           "  -i pssl:[IP]:[PORT]:KEY:CERT:CONTROLLER_CA_CERT\n"
           "                          listen to SSL PORT on interface specified by IP\n"
           "                          (default: 0.0.0.0:%d)\n"
           "\nNetwork control options (must also specify an interface):\n"
           "  -u, --unreliable        do not reconnect to interfaces on error\n",
           program_name, program_name, 6633, 6633);
    //program_name, program_name, OFP_TCP_PORT, OFP_SSL_PORT);
    printf("\nOther options:\n"
           "  -c, --conf=FILE         set configuration file\n"
           "  -d, --daemon            become a daemon\n"
           "  -l, --libdir=DIRECTORY  add a directory to the search path for application libraries\n"
           "  -n, --info=FILE         set controller info file\n"
           "  -p, --pid=FILE          set pid file\n"
           "  -t, --threads=COUNT     set the number of threads\n"
           "  -v, --verbose           set maximum verbosity level (for console)\n"
#ifndef LOG4CXX_ENABLED
           "  -v, --verbose=CONFIG    configure verbosity\n"
#endif
           "  -h, --help              display this help message\n"
           "  -V, --version           display version information\n");
    exit(EXIT_SUCCESS);
}

int daemon()
{
    switch (fork())
    {
    case -1:
        /* Fork failed! */
        return -1;

    case 0:
        /* Daemon process */
        break;

    default:
        /* Valid PID returned, exit the parent process. */
        exit(EXIT_SUCCESS);
        break;
    }

    /* Change the file mode mask */
    umask(0);

    /* Create a new SID for the child process */
    if (setsid() == -1)
    {
        return -1;
    }

    /* Close out the standard file descriptors */
    int fd = open("/dev/null", O_RDWR, 0);
    if (fd != -1)
    {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > 2)
        {
            close(fd);
        }
    }

    return 0;
}

bool verbose = false;
#ifndef LOG4CXX_ENABLED
vector<string> verbosity;
#endif

void init_log()
{
#ifndef LOG4CXX_ENABLED
    //static Vlog_server_socket vlog_server(vlog());
    //vlog_server.listen();
#endif

#ifdef LOG4CXX_ENABLED
    /* Initialize the log4cxx logging infrastructure and override the
       default log level defined in the configuration file with DEBUG
       if 'verbose' set.  */
    ::log4cxx::xml::DOMConfigurator::configureAndWatch("etc/log4cxx.xml");
    if (verbose)
    {
        ::log4cxx::Logger::getRootLogger()->
        setLevel(::log4cxx::Level::getDebug());
    }
#else
    if (verbose)
    {
        set_verbosity(0);
    }
#endif
}

} // unnamed namespace

Disposition remove_pid_file(const char* pid_file, const Event&)
{
    unlink(pid_file);

    return CONTINUE;
}

static void finish_booting(Kernel*, const Application_list&);

// TODO: fix shutdown handling
boost::function<void()> post_shutdown;
void shutdown(int param)
{
	VLOG_ERR(lg, "About to shut down on signal %d", param);
	post_shutdown();
}

int main(int argc, char *argv[])
{
    namespace fs = boost::filesystem;
    /* Program name without full path or "lt-" prefix.  */
    const char *program_name = argv[0];
    if (const char *slash = strrchr(program_name, '/'))
    {
        program_name = slash + 1;
    }
    if (!strncmp(program_name, "lt-", 3))
    {
        program_name += 3;
    }

    const char* pid_file = "/var/run/nox.pid";
    const char* info_file = "./nox.info";
    unsigned int n_threads = 1;
    bool reliable = true;
    bool daemon_flag = false;
    list<string> interfaces;

    string conf = PKGSYSCONFDIR"/nox.meta.json";

    /* Where to look for configuration file.  Check local dir first,
       and then global install dir. */
    list<string> conf_dirs;
    conf_dirs.push_back(PKGSYSCONFDIR"/nox.meta.json");
    conf_dirs.push_back("etc/nox.meta.json");

    /* Add PKGLIBDIRS and local build dir to libdirectory */
    list<string> lib_dirs;
    lib_dirs.push_back("coreapps/");
    lib_dirs.push_back("netapps/");
    lib_dirs.push_back("ext/");

    for (;;)
    {
        static struct option long_options[] =
        {
            {"daemon",      no_argument, 0, 'd'},
            {"unreliable",  no_argument, 0, 'u'},

            {"interface",   required_argument, 0, 'i'},

            {"conf",        required_argument, 0, 'c'},
            {"libdir",      required_argument, 0, 'l'},
            {"pid",         required_argument, 0, 'p'},
            {"threads",     required_argument, 0, 't'},
            {"info",        required_argument, 0, 'n'},
#ifdef LOG4CXX_ENABLED
            {"verbose",     no_argument, 0, 'v'},
#else
            {"verbose",     optional_argument, 0, 'v'},
#endif
            {"help",        no_argument, 0, 'h'},
            {"version",     no_argument, 0, 'V'},
            {0, 0, 0, 0},
        };
        static string short_options
        (long_options_to_short_options(long_options));
        int option_index;
        int c;

        c = getopt_long(argc, argv, short_options.c_str(),
                        long_options, &option_index);
        if (c == -1)
            break;

        switch (c)
        {
        case 'd':
            daemon_flag = true;
            break;

        case 'u':
            reliable = false;
            break;

        case 'i':
            interfaces.push_back(optarg);
            break;

        case 'h':
            usage(program_name);
            break;

        case 'v':
#ifndef LOG4CXX_ENABLED
            if (!optarg)
            {
                verbose = true;
            }
            else
            {
                verbosity.push_back(optarg);
            }
#else
            verbose = true;
#endif
            break;

        case 'c':
            conf_dirs.clear();
            conf_dirs.push_back(optarg);
            break;

        case 'l':
            lib_dirs.push_front(optarg);
            break;

        case 'p':
            pid_file = optarg;
            break;

        case 'n':
            info_file = optarg;
            break;

        case 't':
            n_threads = atoi(optarg);
            break;

        case 'V':
            hello(program_name);
            exit(EXIT_SUCCESS);

        case '?':
            exit(EXIT_FAILURE);

        default:
            abort();
        }
    }

    if (!verbose && !daemon_flag)
    {
        hello(program_name);
    }

    /* Determine the end-user applications to run */
    Application_list applications = parse_application_list(optind, argc, argv);
    std::string platform_config_path;

    try
    {
        /* Parse the platform configuration file */
        BOOST_FOREACH(string s, conf_dirs)
        {
            fs::path confpath(s);
            if (fs::exists(confpath))
            {
                platform_config_path = s;
                break;
            }
        }
    }
    catch (const runtime_error& e)
    {
        printf("ERR: %s", e.what());
        exit(EXIT_FAILURE);
    }

    /* Become a daemon before finishing the booting, if configured */
    if (daemon_flag && daemon())
    {
        exit(EXIT_FAILURE);
    }

    /* Dump stack traces only in a debug or profile build, since only
       then the frame pointers necessary for stack traces are
       available. */
#ifndef NDEBUG
    register_fault_handlers();
#else
#ifdef PROFILING
    register_fault_handlers();
#endif
#endif

    init_log();

#ifndef LOG4CXX_ENABLED
    BOOST_FOREACH (const string& s, verbosity)
    {
        set_verbosity(s.c_str());
    }
#endif

    lg.info("Starting %s (%s)", program_name, argv[0]);

    try
    {
        /* write PID file first */
        if (daemon_flag)
        {
            FILE *f = ::fopen(pid_file, "w");
            if (f)
            {
                fprintf(f, "%ld\n", (long)getpid());
                fclose(f);
                lg.info("wrote pidfile: %s \n", pid_file);
            }
            else
            {
                throw runtime_error("Couldn't create pid file \"" +
                                    string(pid_file) + "\": " +
                                    string(strerror(errno)));
            }
        }

        /* Boot the container */
        Kernel::init(info_file, argc, argv);
        Kernel* kernel = Kernel::get_instance();

        /* Pass the command line arguments for the kernel before
           anything is deployed, so that even the hardcoded components
           could have arguments. */
        BOOST_FOREACH(Application_param app, applications)
        {
            kernel->set_argument(app.first, app.second);
        }

        /* Install the built-in statically linked core components:
           event dispatcher and the DSO deployer. */
        Component_context* event_dispatcher_context =
            new Static_component_context(
            boost::bind(&Event_dispatcher::instantiate, _1, n_threads),
            typeid(Event_dispatcher).name(),
            "event-dispatcher",
            platform_config_path);

        kernel->install(event_dispatcher_context, INSTALLED);

        /* Install the deployer responsible for DSOs. */
        Component_context* dso_deployer_context =
            new Static_component_context(
            boost::bind(&DSO_deployer::instantiate, _1, lib_dirs),
            typeid(DSO_deployer).name(),
            "dso-deployer",
            platform_config_path);

        kernel->install(dso_deployer_context, INSTALLED);

        /* Install the connection manager. */
        Component_context* connection_manager_context =
            new Static_component_context(
            boost::bind(&Connection_manager::instantiate, _1, interfaces),
            typeid(Connection_manager).name(),
            "connection-manager",
            platform_config_path);

        kernel->install(connection_manager_context, INSTALLED);

        /* Finish the booting */
        finish_booting(kernel, applications);

        Event_dispatcher* ed =
            dynamic_cast<Event_dispatcher*>(event_dispatcher_context->get_instance());

		Shutdown_event shutdown_event;
		post_shutdown = 
				boost::bind(&Event_dispatcher::post, ed, shutdown_event);
		signal(SIGTERM, shutdown);
		signal(SIGINT, shutdown);
		signal(SIGHUP, shutdown);
		signal(SIGABRT, shutdown);

        if (daemon_flag)
        {
            /* PID file will be removed just before the daemon
               exits. Register to the shutdown event to catch SIGTERM,
               SIGINT, and SIGHUP based exits. */
			ed->register_handler(Shutdown_event::static_get_name(),
					boost::bind(&remove_pid_file, pid_file, _1),
					9998);
        }

        if (ed) {
            ed->join_all();
		}

    }
    catch (const runtime_error& e)
    {
        lg.err("%s", e.what());
        exit(EXIT_FAILURE);
    }

    return 0;
}

static void finish_booting(Kernel* kernel, const Application_list& applications)
{
    string fatal_error;

    try
    {
        /* Boot the components defined on the command-line */
        BOOST_FOREACH(Application_param app, applications)
        {
            Application_name& name = app.first;
            if (!kernel->get(name))
            {
                kernel->install(name, INSTALLED);
            }
        }
    }
    catch (const runtime_error& e)
    {
        fatal_error = e.what();
    }

    /* Report the installation results. */
    lg.dbg("Application installation report:");
    BOOST_FOREACH(Component_context* ctxt, kernel->get_all())
    {
        lg.dbg("%s:\n%s\n", ctxt->get_name().c_str(),
               ctxt->get_status().c_str());
    }

    /* Check all requested applications have booted. */
    if (fatal_error != "")
    {
        lg.err("%s", fatal_error.c_str());
        exit(EXIT_FAILURE);
    }

    // TODO: FIX THIS
    //nox::post_event(new Bootstrap_complete_event());
    lg.info("nox bootstrap complete");
}
