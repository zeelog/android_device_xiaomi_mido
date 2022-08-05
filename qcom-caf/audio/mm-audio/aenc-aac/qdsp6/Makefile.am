# sources and intermediate files are separated

AM_CFLAGS = -Wall
AM_CFLAGS += -Wundef
AM_CFLAGS += -Wstrict-prototypes
AM_CFLAGS += -Wno-trigraphs
AM_CFLAGS += -g -O3
AM_CFLAGS += -DNDEBUG

AM_CPPFLAGS = -D__packed__=
AM_CPPFLAGS += -DIMAGE_APPS_PROC
AM_CPPFLAGS += -DFEATURE_Q_SINGLE_LINK
AM_CPPFLAGS += -DFEATURE_Q_NO_SELF_QPTR
AM_CPPFLAGS += -DFEATURE_LINUX
AM_CPPFLAGS += -DFEATURE_NATIVELINUX
AM_CPPFLAGS += -DFEATURE_DSM_DUP_ITEMS
AM_CPPFLAGS += -DNDEBUG
AM_CPPFLAGS += -Iinc
AM_CPPFLAGS += -I ${WORKSPACE}/hardware/qcom/media/mm-core/inc/
AM_CPPFLAGS += -I $(PKG_CONFIG_SYSROOT_DIR)/usr/include/audio-kernel

c_sources =src/omx_aac_aenc.cpp
c_sources +=src/aenc_svr.c

lib_LTLIBRARIES = libOmxAacEnc.la
libOmxAacEnc_la_SOURCES = $(c_sources)
libOmxAacEnc_la_CFLAGS = $(AM_CFLAGS) -fPIC $(GLIB_CFLAGS) -include glib.h -Dstrlcpy=g_strlcpy
libOmxAacEnc_la_CPPFLAGS = $(AM_CPPFLAGS) -fPIC $(GLIB_CFLAGS) -include glib.h -Dstrlcpy=g_strlcpy
libOmxAacEnc_la_LIBADD = -lmm-omxcore -lstdc++ -lpthread $(GLIB_LIBS) -ldl -llog
libOmxAacEnc_la_LDFLAGS = -shared $(GLIB_LIBS) -avoid-version $(OMXAUDIO_LIBRARY_VERSION)

bin_PROGRAMS = mm-aenc-omxaac-test
mm_aenc_omxaac_test_SOURCES = ./test/omx_aac_enc_test.c
mm_aenc_omxaac_test_CFLAGS = -include errno.h -include limits.h
mm_aenc_omxaac_test_LDADD = -lmm-omxcore -ldl -lpthread -llog libOmxAacEnc.la
