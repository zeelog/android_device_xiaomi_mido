bin_PROGRAMS = hal_play_test

PLAY_CFLAGS = -Wno-sign-compare -Werror
PLAY_CFLAGS += -D__unused=__attribute__\(\(__unused__\)\)
PLAY_INCLUDES = -I $(top_srcdir)/qahw_api/inc
PLAY_INCLUDES += -I $(top_srcdir)/qahw/inc

hal_play_test_SOURCES = qahw_playback_test.c \
                        qahw_effect_test.c

hal_play_test_LDADD = -lutils ../libqahw.la
AM_CFLAGS = -I $(PKG_CONFIG_SYSROOT_DIR)/usr/include/mm-audio/qahw/inc

if QAP
AM_CFLAGS += -DQAP
AM_CFLAGS += -I ${WORKSPACE}/audio/mm-audio/qap_wrapper/inc/
hal_play_test_SOURCES += qap_wrapper_extn.c
hal_play_test_LDADD += -lqap_wrapper
endif

hal_play_test_CFLAGS = $(PLAY_CFLAGS) $(PLAY_INCLUDES)
hal_play_test_CFLAGS += $(AM_CFLAGS) -DLINUX_ENABLED

bin_PROGRAMS += hal_rec_test

REC_CFLAGS = $(PLAY_CFLAGS) -Dnullptr=NULL
REC_INCLUDES = $(PLAY_INCLUDES)

hal_rec_test_SOURCES = qahw_multi_record_test.c
hal_rec_test_CFLAGS = -Dstrlcat=g_strlcat $(GLIB_CFLAGS) -include glib.h
hal_rec_test_CFLAGS += $(REC_CFLAGS) $(REC_INCLUDES)
hal_rec_test_CFLAGS += $(AM_CFLAGS)
hal_rec_test_LDADD = -lutils ../libqahw.la $(GLIB_LIBS)

bin_PROGRAMS += trans_loopback_test

trans_loopback_test_INCLUDES = -I $(top_srcdir)/qahw_api/inc

trans_loopback_test_SOURCES = trans_loopback_test.c

trans_loopback_test_CFLAGS  = $(AM_CFLAGS) -Wno-sign-compare -Werror
trans_loopback_test_CFLAGS  += $(trans_loopback_test_INCLUDES)
trans_loopback_test_LDADD = -llog  -lutils ../libqahw.la -lcutils -lm

if QAHW_V1
bin_PROGRAMS += hal_voice_test

VOICE_CFLAGS = -Wno-sign-compare -Werror
VOICE_CFLAGS += -D__unused=__attribute__\(\(__unused__\)\)
VOICE_INCLUDES = -I $(top_srcdir)/qahw_api/inc
VOICE_INCLUDES += -I $(top_srcdir)/qahw/inc

hal_voice_test_SOURCES = qahw_voice_test.c

hal_voice_test_LDADD = -lutils ../libqahw.la
AM_CFLAGS = -I $(PKG_CONFIG_SYSROOT_DIR)/usr/include/mm-audio/qahw/inc

if QAP
AM_CFLAGS += -DQAP
AM_CFLAGS += -I ${WORKSPACE}/audio/mm-audio/qap_wrapper/inc/
hal_voice_test_SOURCES += qap_wrapper_extn.c
hal_voice_test_LDADD += -lqap_wrapper
endif

hal_voice_test_CFLAGS = $(VOICE_CFLAGS) $(VOICE_INCLUDES)
hal_voice_test_CFLAGS += $(AM_CFLAGS) -DLINUX_ENABLED
endif
