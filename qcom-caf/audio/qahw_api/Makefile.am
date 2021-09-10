h_sources = inc/qahw_api.h \
            inc/qahw_effect_audiosphere.h \
            inc/qahw_effect_bassboost.h \
            inc/qahw_effect_environmentalreverb.h \
            inc/qahw_effect_equalizer.h \
            inc/qahw_effect_presetreverb.h \
            inc/qahw_effect_virtualizer.h \
            inc/qahw_effect_visualizer.h

AM_CPPFLAGS := -I $(top_srcdir)/qahw_api/inc/
AM_CPPFLAGS += -I $(PKG_CONFIG_SYSROOT_DIR)/usr/include/mm-audio/audio-hal-server
AM_CPPFLAGS += -I $(PKG_CONFIG_SYSROOT_DIR)/usr/include/mm-audio/qahw/inc
AM_CPPFLAGS += -D__unused=__attribute__\(\(__unused__\)\)

library_include_HEADERS = $(h_sources)
library_includedir = $(includedir)/mm-audio/qahw_api/inc

lib_LTLIBRARIES     = libqahw.la
libqahw_la_SOURCES   = src/qahw_api.cpp
libqahw_la_LIBADD    = -lqahwwrapper
libqahw_la_CPPFLAGS := $(AM_CPPFLAGS)
libqahw_la_CPPFLAGS += -std=c++11 -DHAVE_PTHREADS -DHAVE_ANDROID_OS
libqahw_la_CPPFLAGS += -DDEBUG_REFS_CALLSTACK_ENABLED=0
libqahw_la_CPPFLAGS += -DNDEBUG
libqahw_la_CPPFLAGS += $(GLIB_CFLAGS) -include glib.h
libqahw_la_LDFLAGS   = -ltinyalsa -lhardware -lexpat -lcutils -llog -ldl -shared -avoid-version -lpthread -lutils $(GLIB_LIBS)
if QTI_AUDIO_SERVER_ENABLED
AM_CPPFLAGS += -DQTI_AUDIO_SERVER_ENABLED
libqahw_la_LDFLAGS +=  -lqtiaudioserver -lbinder
endif
if QAHW_V1
AM_CPPFLAGS += -DQAHW_V1
endif

