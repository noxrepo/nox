AC_DEFUN([CHECK_NDEBUG], [
AC_ARG_ENABLE(
  [ndebug],
  [AC_HELP_STRING([--enable-ndebug],
                  [Disable debugging features for max performance])],
  [case "${enableval}" in # (
     yes) ndebug=true ;; # (
     no)  ndebug=false ;; # (
     *) AC_MSG_ERROR([bad value ${enableval} for --enable-ndebug]) ;;
   esac],
  [ndebug=false])
AM_CONDITIONAL([NDEBUG], [test x$ndebug = xtrue])
])
