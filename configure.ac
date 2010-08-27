AC_INIT(librouteros, 0.2.3)
AC_CONFIG_SRCDIR(credis.c)
AC_CONFIG_HEADERS(config.h)
AM_INIT_AUTOMAKE(dist-bzip2)
AC_LANG(C)

AC_PREFIX_DEFAULT("/opt/credis")

# API version
LIBCREDIS_MAJOR=`echo $PACKAGE_VERSION | cut -d'.' -f1`
LIBCREDIS_MINOR=`echo $PACKAGE_VERSION | cut -d'.' -f2`
LIBCREDIS_PATCH=`echo $PACKAGE_VERSION | cut -d'.' -f3`
AC_SUBST(LIBCREDIS_MAJOR)
AC_SUBST(LIBCREDIS_MINOR)
AC_SUBST(LIBCREDIS_PATCH)

# ABI version
LIBCREDIS_CURRENT=0
LIBCREDIS_REVISION=0
LIBCREDIS_AGE=0
AC_SUBST(LIBCREDIS_CURRENT)
AC_SUBST(LIBCREDIS_REVISION)
AC_SUBST(LIBCREDIS_AGE)

#
# Check for programs/utilities
#
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AM_CONDITIONAL(COMPILER_IS_GCC, test "x$GCC" = "xyes")

#
# configure libtool
#
AC_LIBTOOL_DLOPEN
AC_PROG_LIBTOOL

#
# Checks for header files.
#
AC_HEADER_STDC
AC_CHECK_HEADERS(arpa/inet.h errno.h fcntl.h netdb.h netinet/in.h netinet/tcp.h sys/select.h sys/time.h sys/socket.h unistd.h assert.h stdarg.h stdio.h, [], [AC_MSG_ERROR("a required header file could not be found")])

socket_needs_socket="no"
AC_CHECK_FUNCS(socket, [],
	AC_CHECK_LIB(socket, socket,,
		AC_MSG_ERROR([cannot find socket(2)])))

AC_ARG_ENABLE(debug, [AS_HELP_STRING([--enable-debug], [Enable debugging output.])],
[
	if test "x$enable_debug" = "xyes"
	then
		AC_DEFINE(WITH_DEBUG, 1, [Define to 1 if you want to get debugging output.])
	fi
], [])
AM_CONDITIONAL(BUILD_WITH_DEBUG, test "x$enable_debug" = "xyes")

AC_OUTPUT(Makefile credis_version.h)
