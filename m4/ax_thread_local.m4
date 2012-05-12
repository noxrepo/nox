AC_DEFUN([CHECK_THREAD_LOCAL], [
AC_MSG_CHECKING([whether thread-local storage with __thread is supported])
AC_LINK_IFELSE(
  [AC_LANG_PROGRAM([
static __thread int x;
#ifdef __NetBSD__
/* Don't use __thread even if compiler and linker said 'yes' on NetBSD. */
choke me
#endif
], [])],
  [AC_MSG_RESULT([yes])
   AC_DEFINE([HAVE_TLS], [1],
             [Define if thread-local storage with __thread is supported])],
  [AC_MSG_RESULT([no])])
])
