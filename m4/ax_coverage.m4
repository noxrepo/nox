AC_DEFUN([CHECK_COVERAGE], [
AC_ARG_ENABLE(
  [coverage],
  [AC_HELP_STRING([--enable-coverage],
                  [Build with gcov code coverage support])],
  [case "${enableval}" in # (
     yes) coverage=true
          CFLAGS="-fprofile-arcs -ftest-coverage $CFLAGS"
          CXXFLAGS="-fprofile-arcs -ftest-coverage $CXXFLAGS"
                        ;; # (
     no)  coverage=false ;; # (
     *) AC_MSG_ERROR([bad value ${enableval} for --enable-coverage]) ;;
   esac],
  [coverage=false])
AM_CONDITIONAL([COVERAGE], [test x$coverage = xtrue])
])
