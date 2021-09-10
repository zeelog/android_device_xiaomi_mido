AM_CFLAGS = -Wall \
            -Wundef \
            -Wstrict-prototypes \
            -Wno-trigraphs \
            -fPIC \
            -g -O3 \
            -DQC_MODIFIED \
            -D_ANDROID_ \
            -D_ENABLE_QC_MSG_LOG_ \
            -DVERBOSE \
            -DNDEBUG \
            -DAUDIOV2 \
            -I inc \
            -I $(PKG_CONFIG_SYSROOT_DIR)/usr/include/audio-kernel \
            -I ${WORKSPACE}/$(call project-path-for,qcom-media)/mm-core/inc/

C_SOURCES = src/aenc_svr.c \
            src/omx_g711_aenc.cpp \
            src/omx_log.cpp

lib_LTLIBRARIES = libOmxG711Enc.la
libOmxG711Enc_la_SOURCES = $(C_SOURCES)
libOmxG711Enc_la_CFLAGS = $(AM_CFLAGS) -Dstrlcpy=g_strlcpy $(GLIB_CFLAGS) -include glib.h
libOmxG711Enc_la_CPPFLAGS = $(AM_CFLAGS) -Dstrlcpy=g_strlcpy $(GLIB_CFLAGS) -include glib.h
libOmxG711Enc_la_LIBADD = $(GLIB_LIBS) -lcutils -llog -lstdc++
libOmxG711Enc_la_LDFLAGS = -shared -avoid-version

bin_PROGRAMS = mm_aenc_omxg711_test
mm_aenc_omxg711_test_SOURCES = ./test/omx_g711_enc_test.c
mm_aenc_omxg711_test_CFLAGS = $(AM_CFLAGS) -Dstrlcpy=g_strlcpy $(GLIB_CFLAGS) -include glib.h
mm_aenc_omxg711_test_LDADD = $(GLIB_LIBS) -lmm-omxcore -lpthread libOmxG711Enc.la
