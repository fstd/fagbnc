#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([fagbnc], [0.0], [van.fisted@googlemail.com])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src/fagbnc.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])

# Checks for programs.
AM_PROG_CC_C_O
AC_PROG_CC_C99
AC_PROG_MAKE_SET

AC_CHECK_LIB([srsirc], [ircbas_init], [], [AC_MSG_ERROR([libsrsirc not installed?])])

AC_CONFIG_FILES([Makefile
                 src/Makefile])
AC_OUTPUT
echo \
"-------------------------------------------------

 ${PACKAGE_NAME} Version ${PACKAGE_VERSION}

 Prefix: '${prefix}'.
 Compiler: '${CC} ${CFLAGS} ${CPPFLAGS}'
 Libraries: '${LIBS}'

--------------------------------------------------"
