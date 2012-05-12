include Make.vars

AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I m4
EXTRA_DIST = \
    LICENSE
SUBDIRS = include lib utilities etc $(ACTIVE_PACKAGES) 

bin_PROGRAMS = nox_core
pkglib_LTLIBRARIES =

nox_core_SOURCES =                          \
    include/component.hh                    \
    include/connection-manager.hh           \
    include/deployer.hh                     \
    include/dso-deployer.hh                 \
    include/kernel.hh                       \
    include/static-deployer.hh              \
    builtin/component.cc                    \
    builtin/connection-manager.cc           \
    builtin/deployer.cc                     \
    builtin/dso-deployer.cc                 \
    builtin/event-dispatcher.cc             \
    builtin/kernel.cc                       \
    builtin/static-deployer.cc              \
    nox_main.cc

CLEANFILES =

nox_core_LDFLAGS =                          \
    $(AM_LDFLAGS) $(BOOST_LDFLAGS) -Wl,-E   \
    -dlopen force

nox_core_LDADD =                            \
    $(LDADD)                                \
    $(builddir)/lib/libnoxcore.la           \
    $(BOOST_THREAD_LIB)                     \
    $(BOOST_FILESYSTEM_LIB)                 \
    $(BOOST_SYSTEM_LIB)                     \
    $(OPENSSL_LIBS)                         \
    $(LIBADD_DL)                            \
    $(LIBADD_DLOPEN)                        \
    $(LIBADD_SHL_LOAD)                      \
    $(LIBLTDL)

nox_core_DEPENDENCIES =

# To link an application library to the nox_core binary, as a
# prerequisite you have to pass '--enable-ltdl' and '--enable-static'
# for the configure. Plus enable the static linkage of the nox_core
# binary by uncommenting the next line:
#nox_core_LDFLAGS += -static
#
# Then to define the application libraries to link to the nox_core
# binary, per library append a) '-dlopen path/to/library.la' to
# nox_core_LDADD and b) 'path/to/library.la' to nox_core_DEPENDENCIES.
# For example:
#nox_core_LDADD += -dlopen nox/coreapps/openflow/openflow.la
#nox_core_DEPENDENCIES += nox/coreapps/openflow/openflow.la

all-local: nox-all-local 
clean-local: nox-clean-local
install-exec-hook: nox-install-local
