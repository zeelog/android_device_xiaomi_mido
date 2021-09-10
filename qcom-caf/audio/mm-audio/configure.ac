#                                               -*- Autoconf -*-

# configure.ac -- Autoconf script for encoders
#

# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([encoders],
	1.0.0)
AM_INIT_AUTOMAKE([foreign])
AM_MAINTAINER_MODE
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

#release versioning
OMXAUDIO_MAJOR_VERSION=1
OMXAUDIO_MINOR_VERSION=0
OMXAUDIO_MICRO_VERSION=0

OMXAUDIO_LIBRARY_VERSION=$OMXAUDIO_MAJOR_VERSION:$OMXAUDIO_MINOR_VERSION:$OMXAUDIO_MICRO_VERSION
AC_SUBST(OMXAUDIO_LIBRARY_VERSION)

AC_ARG_WITH(sanitized-headers,
        AS_HELP_STRING([--with-sanitized-headers=DIR],
                [Specify the location of the sanitized Linux headers]),
        [CPPFLAGS="$CPPFLAGS -idirafter $withval"])

AC_ARG_WITH([glib],
      AC_HELP_STRING([--with-glib],
         [enable glib, Build against glib. Use this when building for HLOS systems which use glib]))

if (test "x${with_glib}" = "xyes"); then
        PKG_CHECK_MODULES(GTHREAD, gthread-2.0 >= 2.16, dummy=yes,
                                AC_MSG_ERROR(GThread >= 2.16 is required))
        PKG_CHECK_MODULES(GLIB, glib-2.0 >= 2.16, dummy=yes,
                                AC_MSG_ERROR(GLib >= 2.16 is required))
        GLIB_CFLAGS="$GLIB_CFLAGS $GTHREAD_CFLAGS"
        GLIB_LIBS="$GLIB_LIBS $GTHREAD_LIBS"

        AC_SUBST(GLIB_CFLAGS)
        AC_SUBST(GLIB_LIBS)
fi

AM_CONDITIONAL(USE_GLIB, test "x${with_glib}" = "xyes")


# Checks for programs.
AC_PROG_CC
AC_PROG_CPP
AC_PROG_CXX
AM_PROG_CC_C_O
AC_PROG_LIBTOOL
AC_PROG_AWK
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET

AC_CONFIG_FILES([     \
	Makefile \
	aenc-aac/Makefile \
	aenc-aac/qdsp6/Makefile \
	aenc-amrnb/Makefile \
	aenc-amrnb/qdsp6/Makefile \
	aenc-g711/Makefile \
	aenc-g711/qdsp6/Makefile
	])
AC_OUTPUT
