include ../../Make.vars 

CONFIGURE_DEPENCIES = $(srcdir)/Makefile.am

OPENFLOW_LIB_VERSION = 1:0:0
OPENFLOW_MANAGER_VERSION = 1:0:0

EXTRA_DIST =                                    \
    meta.json

#lib_LTLIBRARIES = libopenflow.la

#libopenflow_la_CPPFLAGS =                       \
#    $(AM_CPPFLAGS)                              \
#    -I$(top_srcdir)/src/coreapps

#libopenflow_la_SOURCES =

#libopenflow_la_LIBADD =                         \
#    $(BOOST_SERIALIZATION_LIB)

#libopenflow_la_LDFLAGS =                        \
#    $(AM_LDFLAGS) -export-dynamic               \
#    -version-info $(OPENFLOW_LIB_VERSION)

pkglib_LTLIBRARIES =                            \
    openflow_manager.la

openflow_manager_la_CPPFLAGS =                  \
    $(AM_CPPFLAGS)                              \
    -I$(top_srcdir)/src/coreapps

openflow_manager_la_SOURCES =                   \
    openflow-manager.hh                         \
    openflow-manager.cc                         \
    openflow-datapath.hh                        \
    openflow-datapath.cc                        \
    openflow-datapath-join-event.hh             \
    openflow-datapath-leave-event.hh            \
    openflow-event.hh                           \
    openflow-1.0.hh                             \
    openflow-inl-1.0.hh                         \
    openflow-defs-1.0.hh                        \
    openflow-1.0.cc

openflow_manager_la_LDFLAGS =                   \
    $(AM_LDFLAGS) -export-dynamic -module       \
    -version-info $(OPENFLOW_MANAGER_VERSION)

openflow_manager_la_DEPENDENCIES =

openflow_manager_la_LIBADD =

NOX_RUNTIMEFILES = meta.json

all-local: nox-all-local
clean-local: nox-clean-local 
install-exec-hook: nox-install-local
