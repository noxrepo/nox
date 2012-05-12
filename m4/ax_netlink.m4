AC_DEFUN([CHECK_NETLINK], [
AC_CHECK_HEADER([linux/netlink.h],
                [HAVE_NETLINK=yes],
                [HAVE_NETLINK=no],
                [#include <sys/socket.h>])
AM_CONDITIONAL([HAVE_NETLINK], [test "$HAVE_NETLINK" = yes])

if test "x$HAVE_NETLINK" = "xyes"; then
   AC_DEFINE(HAVE_NETLINK,1,[
Provide macro indicating the platform supports netlink
])
fi
])
