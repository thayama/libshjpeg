#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([libshjpeg], [0.0.2], [taki@igel.co.jp])
AC_SUBST([LIBSHJPEG_VERSION], [0:2:0])

AM_INIT_AUTOMAKE([-Wall -Werror])
AC_CONFIG_SRCDIR([src/shjpeg_decode.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AC_CANONICAL_BUILD
AC_CANONICAL_HOST

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_C_INLINE

# Checks for libraries.
AC_CHECK_LIB([jpeg], [jpeg_std_error],, [AC_MSG_ERROR([libjpeg not found!])])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h sys/param.h stdint.h stdlib.h string.h sys/ioctl.h unistd.h jpeglib.h malloc.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_INT8_T
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_CHECK_FUNCS([getpagesize memset munmap strerror])

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 doc/Makefile
		 tests/Makefile])
AC_OUTPUT
