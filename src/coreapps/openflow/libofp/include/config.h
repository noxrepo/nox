/* config.h.  Generated from config.h.in by configure.  */
/* config.h.in.  Generated from configure.ac by autoheader.  */

/* Define if building universal (internal helper macro) */
/* #undef AC_APPLE_UNIVERSAL_BUILD */

/* If the C compiler is GCC 4.7 or later, define to the return value of
   __atomic_always_lock_free(1, 0). If the C compiler is not GCC or is an
   older version of GCC, the value does not matter. */
#define ATOMIC_ALWAYS_LOCK_FREE_1B unsupported

/* If the C compiler is GCC 4.7 or later, define to the return value of
   __atomic_always_lock_free(2, 0). If the C compiler is not GCC or is an
   older version of GCC, the value does not matter. */
#define ATOMIC_ALWAYS_LOCK_FREE_2B unsupported

/* If the C compiler is GCC 4.7 or later, define to the return value of
   __atomic_always_lock_free(4, 0). If the C compiler is not GCC or is an
   older version of GCC, the value does not matter. */
#define ATOMIC_ALWAYS_LOCK_FREE_4B unsupported

/* If the C compiler is GCC 4.7 or later, define to the return value of
   __atomic_always_lock_free(8, 0). If the C compiler is not GCC or is an
   older version of GCC, the value does not matter. */
#define ATOMIC_ALWAYS_LOCK_FREE_8B unsupported

/* Define to 1 to enable time caching, to 0 to disable time caching, or leave
   undefined to use the default (as one should ordinarily do). */
/* #undef CACHE_TIME */

/* Define to 1 if building on ESX. */
/* #undef ESX */

/* Define to 1 if you have backtrace(3). */
#define HAVE_BACKTRACE 1

/* Define to 1 if you have the declaration of `strerror_r', and to 0 if you
   don't. */
#define HAVE_DECL_STRERROR_R 1

/* Define to 1 if you have the declaration of `sys_siglist', and to 0 if you
   don't. */
#define HAVE_DECL_SYS_SIGLIST 1

/* Define to 1 if the C compiler and linker supports the GCC 4.0+ atomic
   built-ins. */
#define HAVE_GCC4_ATOMICS 1

/* Define to 1 if you have the `getloadavg' function. */
#define HAVE_GETLOADAVG 1

/* Define to 1 if you have the `getmntent_r' function. */
#define HAVE_GETMNTENT_R 1

/* Define to 1 if pthread_setname_np() is available and takes 2 parameters
   (like glibc). */
#define HAVE_GLIBC_PTHREAD_SETNAME_NP 1

/* Define to 1 if net/if_dl.h is available. */
/* #undef HAVE_IF_DL */

/* Define to 1 if net/if_packet.h is available. */
#define HAVE_IF_PACKET 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the `socket' library (-lsocket). */
/* #undef HAVE_LIBSOCKET */

/* Define to 1 if you have the <linux/if_ether.h> header file. */
#define HAVE_LINUX_IF_ETHER_H 1

/* Define to 1 if you have the <linux/types.h> header file. */
#define HAVE_LINUX_TYPES_H 1

/* Define to 1 if you have __malloc_hook, __realloc_hook, and __free_hook in
   <malloc.h>. */
#define HAVE_MALLOC_HOOKS 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the `mlockall' function. */
#define HAVE_MLOCKALL 1

/* Define to 1 if you have the <mntent.h> header file. */
#define HAVE_MNTENT_H 1

/* Define to 1 if pthread_setname_np() is available and takes 3 parameters
   (like NetBSD). */
/* #undef HAVE_NETBSD_PTHREAD_SETNAME_NP */

/* Define to 1 if Netlink protocol is available. */
#define HAVE_NETLINK 1

/* Define to 1 if you have the <net/if_mib.h> header file. */
/* #undef HAVE_NET_IF_MIB_H */

/* Define to 1 if OpenSSL is installed. */
#define HAVE_OPENSSL 1

/* Define to 1 if you have the `pthread_set_name_np' function. */
/* #undef HAVE_PTHREAD_SET_NAME_NP */

/* Define to 1 if you have the `statvfs' function. */
#define HAVE_STATVFS 1

/* Define to 1 if you have the <stdatomic.h> header file. */
/* #undef HAVE_STDATOMIC_H */

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the `strerror_r' function. */
#define HAVE_STRERROR_R 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the `strnlen' function. */
#define HAVE_STRNLEN 1

/* Define if strtok_r macro segfaults on some inputs */
/* #undef HAVE_STRTOK_R_BUG */

/* Define to 1 if `struct ifreq' is a member of `ifr_flagshigh'. */
/* #undef HAVE_STRUCT_IFREQ_IFR_FLAGSHIGH */

/* Define to 1 if `struct stat' is a member of `st_mtimensec'. */
/* #undef HAVE_STRUCT_STAT_ST_MTIMENSEC */

/* Define to 1 if `struct stat' is a member of `st_mtim.tv_nsec'. */
#define HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC 1

/* Define to 1 if you have the <sys/statvfs.h> header file. */
#define HAVE_SYS_STATVFS_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if the C compiler and linker supports the C11 thread_local
   matcro defined in <threads.h>. */
/* #undef HAVE_THREAD_LOCAL */

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the <valgrind/valgrind.h> header file. */
/* #undef HAVE_VALGRIND_VALGRIND_H */

/* Define to 1 if the C compiler and linker supports the GCC __thread
   extenions. */
#define HAVE___THREAD 1

/* System uses the linux datapath module. */
#define LINUX_DATAPATH 1

/* Define to 1 if your C compiler doesn't accept -c and -o together. */
/* #undef NO_MINUS_C_MINUS_O */

/* Name of package */
#define PACKAGE "openvswitch"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "ovs-bugs@openvswitch.org"

/* Define to the full name of this package. */
#define PACKAGE_NAME "openvswitch"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "openvswitch 2.0.0"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "openvswitch"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "2.0.0"

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Define to 1 if strerror_r returns char *. */
#define STRERROR_R_CHAR_P 1

/* Define to 1 if the compiler support putting variables into sections with
   user-defined names and the linker automatically defines __start_SECNAME and
   __stop_SECNAME symbols that designate the start and end of the section. */
#define USE_LINKER_SECTIONS 1

/* Enable extensions on AIX 3, Interix.  */
#ifndef _ALL_SOURCE
# define _ALL_SOURCE 1
#endif
/* Enable GNU extensions on systems that have them.  */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif
/* Enable threading extensions on Solaris.  */
#ifndef _POSIX_PTHREAD_SEMANTICS
# define _POSIX_PTHREAD_SEMANTICS 1
#endif
/* Enable extensions on HP NonStop.  */
#ifndef _TANDEM_SOURCE
# define _TANDEM_SOURCE 1
#endif
/* Enable general extensions on Solaris.  */
#ifndef __EXTENSIONS__
# define __EXTENSIONS__ 1
#endif


/* Version number of package */
#define VERSION "2.0.0"

/* Define WORDS_BIGENDIAN to 1 if your processor stores words with the most
   significant byte first (like Motorola and SPARC, unlike Intel). */
#if defined AC_APPLE_UNIVERSAL_BUILD
# if defined __BIG_ENDIAN__
#  define WORDS_BIGENDIAN 1
# endif
#else
# ifndef WORDS_BIGENDIAN
/* #  undef WORDS_BIGENDIAN */
# endif
#endif

/* Number of bits in a file offset, on hosts where this is settable. */
/* #undef _FILE_OFFSET_BITS */

/* Define for large files, on AIX-style hosts. */
/* #undef _LARGE_FILES */

/* Define to 1 if on MINIX. */
/* #undef _MINIX */

/* Define to 2 if the system does not provide POSIX.1 features except with
   this defined. */
/* #undef _POSIX_1_SOURCE */

/* Define to 1 if you need to in order for `stat' and other things to work. */
/* #undef _POSIX_SOURCE */
