AM_CFLAGS = -I $(top_srcdir)/inc

h_sources = inc/qahw.h \
            inc/qahw_effect_api.h

qahw_include_HEADERS = $(h_sources)
qahw_includedir = $(includedir)/mm-audio/qahw/inc

qahw_api_include_HEADERS = inc/qahw_defs.h
qahw_api_includedir = $(includedir)/mm-audio/qahw_api/inc

lib_LTLIBRARIES = libqahwwrapper.la
libqahwwrapper_la_SOURCES = src/qahw.c \
                     src/qahw_effect.c

if SVA_AUDIO_CONCURRENCY
AM_CFLAGS += -DSVA_AUDIO_CONC
endif

if QAHW_V1
AM_CFLAGS += -DQAHW_V1
endif

libqahwwrapper_la_CFLAGS  = $(AM_CFLAGS)
libqahwwrapper_la_CFLAGS += -include stddef.h
libqahwwrapper_la_CFLAGS += -Dstrlcpy=g_strlcpy $(GLIB_CFLAGS) -include glib.h
libqahwwrapper_la_CFLAGS += -D__unused=__attribute__\(\(__unused__\)\)
libqahwwrapper_la_CFLAGS += -Werror -Wall
libqahwwrapper_la_CFLAGS += -DNDEBUG
libqahwwrapper_la_LDFLAGS = -shared -avoid-version -llog -lcutils -lhardware $(GLIB_LIBS)
