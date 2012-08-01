include ../Make.vars

CONFIGURE_DEPENCIES = $(srcdir)/Makefile.am

NOXCORE_LIB_VERSION = 1:0:0

lib_LTLIBRARIES = libnoxcore.la

EXTRA_DIST =                                                \
    dh1024.pem                                              \
    dh2048.pem                                              \
    dh4096.pem

libnoxcore_la_SOURCES =                                     \
    command-line.cc                                         \
    connection.cc                                           \
    dhparams.h                                              \
    errno_exception.cc                                      \
    fault.cc                                                \
    network_iarchive.cc                                     \
    network_oarchive.cc                                     \
    sigset.cc                                               \
    string.cc                                               \
    timeval.cc                                              \
    vlog.cc

nodist_libnoxcore_la_SOURCES =                              \
    dhparams.c

libnoxcore_la_LIBADD =                                      \
    $(BOOST_FILESYSTEM_LIB)                                 \
    $(BOOST_SYSTEM_LIB)                                     \
    $(BOOST_SERIALIZATION_LIB)                              \
    $(BOOST_THREAD_LIB)                                     \
    $(BOOST_UNIT_TEST_FRAMEWORK_LIB)                        \
    $(OPENSSL_LIBS)

libnoxcore_la_LDFLAGS =                                     \
	$(AM_LDFLAGS) -version-info $(NOXCORE_LIB_VERSION)

dhparams.c: dh1024.pem dh2048.pem dh4096.pem
	(echo '#include "dhparams.h"' &&                        \
     openssl dhparam -C -in $(srcdir)/dh1024.pem -noout &&  \
     openssl dhparam -C -in $(srcdir)/dh2048.pem -noout &&  \
     openssl dhparam -C -in $(srcdir)/dh4096.pem -noout)    \
    | sed 's/\(get_dh[0-9]*\)()/\1(void)/' > dhparams.c.tmp
	mv dhparams.c.tmp dhparams.c

install-exec-hook:                                          \
    @list='$(lib_LTLIBRARIES)'; for p in $$list; do         \
      if test -f $$p; then                                  \
        f=$(am__strip_dir)                                  \
        echo $(DESTDIR)$(libdir)/$$f                        \
      else :; fi;                                           \
    done
