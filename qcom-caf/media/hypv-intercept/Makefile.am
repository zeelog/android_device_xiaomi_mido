# -----------------------------------------------------------------------------
#               Common definitons
# -----------------------------------------------------------------------------

AM_CFLAGS = -g -O3
AM_CFLAGS += -Werror

AM_CPPFLAGS = -D_LINUX_
AM_CPPFLAGS += -I$(top_srcdir)/mm-video-v4l2/vidc/common/inc

# -----------------------------------------------------------------------------
#           Make the Shared library (libhypv_intercept)
# -----------------------------------------------------------------------------
h_sources = hypv_intercept.h
c_sources = hypv_intercept.cpp

include_HEADERS = $(h_source)
lib_LTLIBRARIES = libhypv_intercept.la
libhypv_intercept_la_SOURCES = $(c_sources)
libhypv_intercept_la_CFLAGS = $(AM_CFLAGS) -fPIC
libhypv_intercept_la_CPPFLAGS = $(AM_CPPFLAGS)
libhypv_intercept_la_LDFLAGS = -shared -llog -ldl
