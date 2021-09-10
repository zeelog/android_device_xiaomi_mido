h_sources = qdMetaData.h

cpp_sources = qdMetaData.cpp

library_includedir = $(includedir)
library_include_HEADERS = $(h_sources)

lib_LTLIBRARIES = libqdMetaData.la
libqdMetaData_la_CC = @CC@
libqdMetaData_la_SOURCES = $(cpp_sources)
libqdMetaData_la_CFLAGS = $(AM_CFLAGS) -DLOG_TAG=\"DisplayMetaData\"
libqdMetaData_la_CPPFLAGS = $(AM_CPPFLAGS)
libqdMetaData_LDADD = -lcutils -llog
libqdMetaData_la_LDFLAGS = -shared -avoid-version

header_sources = display_config.h

c_sources = profiler.cpp \
            qd_utils.cpp \
            display_config.cpp

library_includedir = $(includedir)
library_include_HEADERS = $(header_sources)

lib_LTLIBRARIES += libqdutils.la
libqdutils_la_CC = @CC@
libqdutils_la_SOURCES = $(c_sources)
libqdutils_la_CFLAGS = $(COMMON_CFLAGS) -DLOG_TAG=\"qdutils\"
libqdutils_la_CPPFLAGS = $(AM_CPPFLAGS)
libqdutils_LDADD = -lhardware -lcutils -llog -lbinder
libqdutils_la_LIBADD = ../libqservice/libqservice.la
libqdutils_la_LDFLAGS = -shared -avoid-version