HEADER_PATH := ${WORKSPACE}/display/display-hal/include
h_sources = alloc_controller.h \
            memalloc.h

cpp_sources = ionalloc.cpp \
              alloc_controller.cpp

memalloc_includedir = $(pkgincludedir)
memalloc_include_HEADERS = $(h_sources)

lib_LTLIBRARIES = libmemalloc.la
libmemalloc_la_CC = @CC@
libmemalloc_la_SOURCES = $(cpp_sources)
libmemalloc_la_CFLAGS = $(COMMON_CFLAGS) -DLOG_TAG=\"qdmemalloc\"
libmemalloc_la_CPPFLAGS = $(AM_CPPFLAGS)
libmemalloc_la_LIBADD = ../libqdutils/libqdutils.la
libmemalloc_la_LIBADD += -lhardware -lcutils -llog -lutils -ldl
libmemalloc_la_LDFLAGS = -shared -avoid-version

header_sources = gralloc_priv.h \
                 gr.h \
                 adreno_utils.h \
                 $(HEADER_PATH)/color_metadata.h

c_sources = gpu.cpp \
            gralloc.cpp \
            framebuffer.cpp \
            mapper.cpp

library_includedir = $(pkgincludedir)
library_include_HEADERS = $(header_sources)

lib_LTLIBRARIES += libgralloc.la
libgralloc_la_CC = @CC@
libgralloc_la_SOURCES = $(c_sources)
libgralloc_la_CFLAGS = $(COMMON_CFLAGS) -DLOG_TAG=\"qdgralloc\"
libgralloc_la_CPPFLAGS = $(AM_CPPFLAGS)
libgralloc_la_LIBADD = ../libqdutils/libqdutils.la
libgralloc_la_LIBADD += ../libqdutils/libqdMetaData.la
libgralloc_la_LIBADD += -lhardware -lcutils -llog -lutils -lbinder
libgralloc_la_LIBADD += libmemalloc.la
libgralloc_la_LDFLAGS = -shared -avoid-version
