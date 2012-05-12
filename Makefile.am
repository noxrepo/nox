AUTOMAKE_OPTIONS = foreign 1.10
ACLOCAL_AMFLAGS = -I m4

SUBDIRS = \
	src \
	$(subdirs)

include doc/doxygen.am

dist-local:

nox_installsubdir = $(DESTDIR)$(subst src,$(bindir),$(subdir))

install-exec-hook:

clean-local:
	rm -rf __nox_tests
