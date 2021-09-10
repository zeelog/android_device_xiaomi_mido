AM_CFLAGS = -I ${WORKSPACE}/external/tinyalsa/include \
        -I ${WORKSPACE}/system/media/audio_effects/include \
        -I ${WORKSPACE}/system/media/audio_utils/include \
        -I $(top_srcdir)/hal \
        -I $(top_srcdir)/hal/audio_extn \
        -I $(top_srcdir)/hal/voice_extn \
        -I $(PKG_CONFIG_SYSROOT_DIR)/usr/include/audio-kernel \
        -I $(top_srcdir)/hal/${TARGET_PLATFORM}

c_sources = audio_hw.c \
            voice.c \
            platform_info.c \
            ${TARGET_PLATFORM}/platform.c \
            audio_extn/audio_extn.c \
            audio_extn/utils.c \
            acdb.c

if HDMI_EDID
AM_CFLAGS += -DHDMI_EDID
c_sources += edid.c
endif

if FM_POWER_OPT
AM_CFLAGS += -DFM_POWER_OPT
c_sources += audio_extn/fm.c
endif

if USBAUDIO
AM_CFLAGS += -DUSB_TUNNEL_ENABLED
c_sources += audio_extn/usb.c
endif

if HFP
AM_CFLAGS += -DHFP_ENABLED
c_sources += audio_extn/hfp.c
endif

if SSR
AM_CFLAGS += -DSSR_ENABLED
c_sources += audio_extn/ssr.c
AM_CFLAGS +=  -I ${WORKSPACE}/audio/mm-audio-external-noship/surround_sound_3mic/libsurround_3mic_proc/surround_rec_interface/inc/
endif

if MULTI_VOICE_SESSIONS
AM_CFLAGS += -DMULTI_VOICE_SESSION_ENABLED
c_sources += voice_extn/voice_extn.c
endif

if COMPRESS_VOIP
AM_CFLAGS += -DCOMPRESS_VOIP_ENABLED
c_sources += voice_extn/compress_voip.c
endif

if SPKR_PROTECTION
AM_CFLAGS += -DSPKR_PROT_ENABLED
c_sources += audio_extn/spkr_protection.c
endif

if HW_VARIANTS
AM_CFLAGS += -DHW_VARIANTS_ENABLED
c_sources +=  ${TARGET_PLATFORM}/hw_info.c
endif

if COMPRESS_CAPTURE
AM_CFLAGS += -DCOMPRESS_CAPTURE_ENABLED
c_sources += audio_extn/compress_capture.c
endif

if DTS_EAGLE
AM_CFLAGS += -DDTS_EAGLE
c_sources += audio_extn/dts_eagle.c
endif

if DOLBY_DDP
AM_CFLAGS += -DDS1_DOLBY_DDP_ENABLED
c_sources += audio_extn/dolby.c
endif

if DS1_DOLBY_DAP
AM_CFLAGS += -DDS1_DOLBY_DAP_ENABLED
if DOLBY_DDP
c_sources += audio_extn/dolby.c
endif
endif

if DEV_ARBI
AM_CFLAGS += -DDEV_ARBI_ENABLED
c_sources += audio_extn/dev_arbi.c
endif

if SOURCE_TRACKING
AM_CFLAGS += -DSOURCE_TRACKING_ENABLED
c_sources += audio_extn/source_track.c
endif

#if LISTEN
#AM_CFLAGS += -DAUDIO_LISTEN_ENABLED
#AM_CFLAGS += -I ${WORKSPACE}/audio/mm-audio-external-noship/audio-listen
#c_sources += audio_extn/listen.c
#endif

if SOUND_TRIGGER
AM_CFLAGS += -DSOUND_TRIGGER_ENABLED
AM_CFLAGS += -I ${WORKSPACE}/audio/mm-audio/sound_trigger/
c_sources += audio_extn/soundtrigger.c
endif

if FLAC_SUPPORT
AM_CFLAGS += -DFLAC_OFFLOAD_ENABLED
AM_CFLAGS += -DCOMPRESS_METADATA_NEEDED
endif

if ALAC_SUPPORT
AM_CFLAGS += -DALAC_OFFLOAD_ENABLED
AM_CFLAGS += -DCOMPRESS_METADATA_NEEDED
endif

if VORBIS_SUPPORT
AM_CFLAGS += -DVORBIS_OFFLOAD_ENABLED
AM_CFLAGS += -DCOMPRESS_METADATA_NEEDED
endif

if WMA_SUPPORT
AM_CFLAGS += -DWMA_OFFLOAD_ENABLED
AM_CFLAGS += -DCOMPRESS_METADATA_NEEDED
endif

if COMPRESS_INPUT
AM_CFLAGS += -DCOMPRESS_INPUT_ENABLED
c_sources += audio_extn/compress_in.c
endif

if COMPRESS_PASSTHROUGH
AM_CFLAGS += -DHDMI_PASSTHROUGH_ENABLED
c_sources += audio_extn/passthru.c
endif

if AUDIO_PARSER
AM_CFLAGS += -I ${WORKSPACE}/audio/mm-audio/audio-parsers/inc/
if DTSHD_PARSER
AM_CFLAGS += -DDTSHD_PARSER_ENABLED
endif
endif

if KEEP_ALIVE
AM_CFLAGS += -DKEEP_ALIVE_ENABLED
c_sources += audio_extn/keep_alive.c
endif

if AUDIO_HW_EXTN_API
AM_CFLAGS += -DAUDIO_HW_EXTN_API_ENABLED
c_sources += audio_hw_extn_api.c
endif

if GEF
AM_CFLAGS += -DAUDIO_GENERIC_EFFECT_FRAMEWORK_ENABLED
c_sources += audio_extn/gef.c
endif

if APTX_DECODER
AM_CFLAGS += -DAPTX_DECODER_ENABLED
endif

if AFE_PROXY
AM_CFLAGS += -DAFE_PROXY_ENABLED
endif

if ADSP_HDLR
AM_CFLAGS += -DAUDIO_EXTN_ADSP_HDLR_ENABLED
c_sources += audio_extn/adsp_hdlr.c
endif

if A2DP_OFFLOAD
AM_CFLAGS += -DA2DP_OFFLOAD_ENABLED
c_sources += audio_extn/a2dp.c
endif

if AUDIO_IP_HDLR
AM_CFLAGS += -DAUDIO_EXTN_IP_HDLR_ENABLED
c_sources += audio_extn/ip_hdlr_intf.c
endif

if QAF_SUPPORT
AM_CFLAGS += -DQAF_EXTN_ENABLED
c_sources += audio_extn/qaf.c
endif

if AUDIO_HW_LOOPBACK
AM_CFLAGS += -DAUDIO_HW_LOOPBACK_ENABLED
AM_CFLAGS += -DCOMPRESS_METADATA_NEEDED
c_sources += audio_extn/hw_loopback.c
endif

if RUN_KEEP_ALIVE_IN_ARM_FFV
AM_CFLAGS += -DRUN_KEEP_ALIVE_IN_ARM_FFV
endif

if AUDIO_HW_FFV
AM_CFLAGS += -DFFV_ENABLED \
             -I $(PKG_CONFIG_SYSROOT_DIR)/usr/include/ffv/
c_sources += audio_extn/ffv.c
endif

if CUSTOM_STEREO
AM_CFLAGS += -DCUSTOM_STEREO_ENABLED
endif

if INSTANCE_ID
AM_CFLAGS += -DINSTANCE_ID_ENABLED
endif

if LL_AS_PRIMARY_OUTPUT
AM_CFLAGS += -DUSE_LL_AS_PRIMARY_OUTPUT
endif

h_sources = audio_extn/audio_defs.h \
            audio_extn/audio_extn.h \
            audio_hw.h \
            voice.h

library_include_HEADERS = $(h_sources)
library_includedir = $(includedir)

lib_LTLIBRARIES = audio.primary.default.la
audio_primary_default_la_SOURCES = $(c_sources)
audio_primary_default_la_LIBADD = $(GLIB_LIBS) -llog -lcutils -ltinyalsa
audio_primary_default_la_LIBADD += -ltinycompress -laudioroute -ldl -lexpat -laudioutils
if AUDIO_PARSER
audio_primary_default_la_LIBADD += -laudioparsers
endif
audio_primary_default_la_CFLAGS = $(AM_CFLAGS) $(GLIB_CFLAGS)
audio_primary_default_la_CFLAGS += -Dstrlcat=g_strlcat
audio_primary_default_la_CFLAGS += -DINT_MAX=0x7fffffff
audio_primary_default_la_CFLAGS += -D__unused=__attribute__\(\(__unused__\)\)
audio_primary_default_la_CFLAGS += -DLINUX_ENABLED $(TARGET_CFLAGS) -DAUDIO_EXTN_FORMATS_ENABLED
audio_primary_default_la_CFLAGS += -DNDEBUG
audio_primary_default_la_LDFLAGS = -module -shared -avoid-version
