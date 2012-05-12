AC_DEFUN([CHECK_PROFILING], [
AC_ARG_ENABLE(
  [profiling],
  [AC_HELP_STRING([--enable-profiling],
                  [Build with gprof profiling support])],
  [case "${enableval}" in # (
     yes) profiling=true
          CFLAGS="-pg $CFLAGS"
          CXXFLAGS="-pg $CXXFLAGS"
                        ;; # (
     no)  profiling=false ;; # (
     *) AC_MSG_ERROR([bad value ${enableval} for --enable-profiling]) ;;
   esac],
  [profiling=false])
AM_CONDITIONAL([PROFILING], [test x$profiling = xtrue])
])
