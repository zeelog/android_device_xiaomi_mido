cpp_sources = debug.cpp \
              rect.cpp \
              sys.cpp \
              formats.cpp \
              utils.cpp

lib_LTLIBRARIES = libsdmutils.la
libsdmutils_la_CC = @CC@
libsdmutils_la_SOURCES = $(cpp_sources)
libsdmutils_la_CFLAGS = $(COMMON_CFLAGS) -DLOG_TAG=\"SDM\"
libsdmutils_la_CPPFLAGS = $(AM_CPPFLAGS)
libsdmutils_la_LDFLAGS = -shared -avoid-version
