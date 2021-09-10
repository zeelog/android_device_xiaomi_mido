ifneq ($(AUDIO_USE_STUB_HAL), true)
ifeq ($(strip $(BOARD_USES_ALSA_AUDIO)),true)
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

AUDIO_PLATFORM := $(TARGET_BOARD_PLATFORM)

ifneq ($(filter msm8974 msm8226 msm8084 msm8610 apq8084 msm8994 msm8992 msm8996 msm8998 apq8098_latv sdm845 sdm710 qcs605 sdmshrike msmnile kona lahaina holi sdm660 msm8937 msm8953 $(MSMSTEPPE) $(TRINKET) lito atoll bengal,$(TARGET_BOARD_PLATFORM)),)
  # B-family platform uses msm8974 code base
  AUDIO_PLATFORM = msm8974
  MULTIPLE_HW_VARIANTS_ENABLED := true
ifneq ($(filter msm8974,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSM8974
  LOCAL_CFLAGS += -DMAX_TARGET_SPECIFIC_CHANNEL_CNT="2"
endif
ifneq ($(filter msm8610,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSM8610
endif
ifneq ($(filter msm8226,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSM8x26
  LOCAL_CFLAGS += -DMAX_TARGET_SPECIFIC_CHANNEL_CNT="2"
endif
ifneq ($(filter msm8084,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSM8084
  LOCAL_CFLAGS += -DMAX_TARGET_SPECIFIC_CHANNEL_CNT="2"
endif
ifneq ($(filter apq8084,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_APQ8084
endif
ifneq ($(filter msm8994,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSM8994
  LOCAL_CFLAGS += -DMAX_TARGET_SPECIFIC_CHANNEL_CNT="4"
  LOCAL_CFLAGS += -DKPI_OPTIMIZE_ENABLED
endif
ifneq ($(filter msm8992,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSM8994
  LOCAL_CFLAGS += -DMAX_TARGET_SPECIFIC_CHANNEL_CNT="4"
  LOCAL_CFLAGS += -DKPI_OPTIMIZE_ENABLED
endif
ifneq ($(filter msm8996,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSM8996
  LOCAL_CFLAGS += -DMAX_TARGET_SPECIFIC_CHANNEL_CNT="4"
  LOCAL_CFLAGS += -DKPI_OPTIMIZE_ENABLED
  LOCAL_CFLAGS += -DINCALL_MUSIC_ENABLED
endif
ifneq ($(filter msm8998 apq8098_latv,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSM8998
  LOCAL_CFLAGS += -DMAX_TARGET_SPECIFIC_CHANNEL_CNT="8"
  LOCAL_CFLAGS += -DINCALL_MUSIC_ENABLED
endif
ifneq ($(filter sdm845,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_SDM845
  LOCAL_CFLAGS += -DMAX_TARGET_SPECIFIC_CHANNEL_CNT="8"
  LOCAL_CFLAGS += -DINCALL_MUSIC_ENABLED
  LOCAL_CFLAGS += -DINCALL_STEREO_CAPTURE_ENABLED
endif
ifneq ($(filter sdm710,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_SDM710
  LOCAL_CFLAGS += -DMAX_TARGET_SPECIFIC_CHANNEL_CNT="8"
endif
ifneq ($(filter qcs605,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_QCS605
endif
ifneq ($(filter msmnile sdmshrike,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSMNILE
  LOCAL_CFLAGS += -DMAX_TARGET_SPECIFIC_CHANNEL_CNT="4"
  LOCAL_CFLAGS += -DINCALL_MUSIC_ENABLED
  LOCAL_CFLAGS += -DINCALL_STEREO_CAPTURE_ENABLED
endif
ifneq ($(filter kona lahaina,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_KONA
  LOCAL_CFLAGS += -DMAX_TARGET_SPECIFIC_CHANNEL_CNT="4"
  LOCAL_CFLAGS += -DINCALL_STEREO_CAPTURE_ENABLED
endif
ifneq ($(filter $(MSMSTEPPE) ,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSMSTEPPE
  LOCAL_CFLAGS += -DMAX_TARGET_SPECIFIC_CHANNEL_CNT="4"
endif
ifneq ($(filter $(TRINKET) ,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_TRINKET
  LOCAL_CFLAGS += -DMAX_TARGET_SPECIFIC_CHANNEL_CNT="4"
endif
ifneq ($(filter lito,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_LITO
  LOCAL_CFLAGS += -DMAX_TARGET_SPECIFIC_CHANNEL_CNT="4"
  LOCAL_CFLAGS += -DINCALL_STEREO_CAPTURE_ENABLED
endif
ifneq ($(filter holi,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_HOLI
  LOCAL_CFLAGS += -DMAX_TARGET_SPECIFIC_CHANNEL_CNT="4"
  LOCAL_CFLAGS += -DINCALL_STEREO_CAPTURE_ENABLED
endif
ifneq ($(filter bengal,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_BENGAL
  LOCAL_CFLAGS += -DMAX_TARGET_SPECIFIC_CHANNEL_CNT="4"
  LOCAL_CFLAGS += -DINCALL_STEREO_CAPTURE_ENABLED
endif
ifneq ($(filter atoll,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_ATOLL
  LOCAL_CFLAGS += -DMAX_TARGET_SPECIFIC_CHANNEL_CNT="4"
  LOCAL_CFLAGS += -DINCALL_STEREO_CAPTURE_ENABLED
endif
ifneq ($(filter sdm660,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSMFALCON
  LOCAL_CFLAGS += -DMAX_TARGET_SPECIFIC_CHANNEL_CNT="8"
endif
ifneq ($(filter msm8937,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSM8937
  LOCAL_CFLAGS += -DMAX_TARGET_SPECIFIC_CHANNEL_CNT="8"
endif
ifneq ($(filter msm8953,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSM8953
  LOCAL_CFLAGS += -DMAX_TARGET_SPECIFIC_CHANNEL_CNT="8"
endif
endif

ifeq ($(TARGET_BOARD_AUTO),true)
  LOCAL_CFLAGS += -DPLATFORM_AUTO
endif

LOCAL_CFLAGS += -Wno-macro-redefined

LOCAL_HEADER_LIBRARIES := libhardware_headers

LOCAL_SRC_FILES := \
    audio_hw.c \
    acdb.c \
    platform_info.c \
    $(AUDIO_PLATFORM)/platform.c \
    voice.c

LOCAL_SRC_FILES += audio_extn/audio_extn.c \
                   audio_extn/audio_hidl.cpp \
                   audio_extn/compress_in.c \
                   audio_extn/fm.c \
                   audio_extn/keep_alive.c \
                   audio_extn/source_track.c \
                   audio_extn/usb.c \
                   audio_extn/utils.c \
                   audio_extn/device_utils.c \
                   voice_extn/compress_voip.c \
                   voice_extn/voice_extn.c

LOCAL_SHARED_LIBRARIES := \
    libbase \
    liblog \
    libcutils \
    libtinyalsa \
    libtinycompress \
    libaudioroute \
    libdl \
    libaudioutils \
    libexpat \
    libhidlbase \
    libprocessgroup \
    libutils

LOCAL_C_INCLUDES += \
    external/tinyalsa/include \
    external/tinycompress/include \
    system/media/audio_utils/include \
    external/expat/lib \
    vendor/qcom/opensource/core-utils/fwk-detect \
    $(call include-path-for, audio-route) \
    $(call include-path-for, audio-effects) \
    $(LOCAL_PATH)/$(AUDIO_PLATFORM) \
    $(LOCAL_PATH)/audio_extn \
    $(LOCAL_PATH)/voice_extn

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/audio
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/techpack/audio/include
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr


# Hardware specific feature
ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DLKM)),true)
  LOCAL_HEADER_LIBRARIES += audio_kernel_headers
  LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/vendor/qcom/opensource/audio-kernel/include
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_EXTENDED_COMPRESS_FORMAT)),true)
  LOCAL_CFLAGS += -DENABLE_EXTENDED_COMPRESS_FORMAT
endif

LOCAL_CFLAGS += -DUSE_VENDOR_EXTN

# Legacy feature
ifdef MULTIPLE_HW_VARIANTS_ENABLED
  LOCAL_CFLAGS += -DHW_VARIANTS_ENABLED
  LOCAL_SRC_FILES += $(AUDIO_PLATFORM)/hw_info.c
endif

# Legacy feature
ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DTS_EAGLE)),true)
    LOCAL_CFLAGS += -DDTS_EAGLE
    LOCAL_SRC_FILES += audio_extn/dts_eagle.c
endif

# Legacy feature
ifeq ($(strip $(DOLBY_DDP)),true)
    LOCAL_CFLAGS += -DDS1_DOLBY_DDP_ENABLED
    LOCAL_SRC_FILES += audio_extn/dolby.c
endif

# Legacy feature
ifeq ($(strip $(DS1_DOLBY_DAP)),true)
    LOCAL_CFLAGS += -DDS1_DOLBY_DAP_ENABLED
ifneq ($(strip $(DOLBY_DDP)),true)
    LOCAL_SRC_FILES += audio_extn/dolby.c
endif
endif

# Legacy feature
ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DS2_DOLBY_DAP)),true)
    LOCAL_CFLAGS += -DDS2_DOLBY_DAP_ENABLED
    LOCAL_CFLAGS += -DDS1_DOLBY_DDP_ENABLED
ifneq ($(strip $(DOLBY_DDP)),true)
    ifneq ($(strip $(DS1_DOLBY_DAP)),true)
        LOCAL_SRC_FILES += audio_extn/dolby.c
    endif
endif
endif

# NonLA feature
ifeq ($(strip $(AUDIO_FEATURE_ENABLED_QAF)),true)
    LOCAL_CFLAGS += -DQAF_EXTN_ENABLED
    LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/qaf/
    LOCAL_SRC_FILES += audio_extn/qaf.c
endif

# Hardware specific feature
ifeq ($(strip $(AUDIO_FEATURE_ENABLED_QAP)),true)
LOCAL_CFLAGS += -DQAP_EXTN_ENABLED -Wno-tautological-pointer-compare
LOCAL_SRC_FILES += audio_extn/qap.c
LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/qap_wrapper/
LOCAL_HEADER_LIBRARIES += audio_qaf_headers
LOCAL_SHARED_LIBRARIES += libqap_wrapper liblog
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_LISTEN)),true)
    LOCAL_CFLAGS += -DAUDIO_LISTEN_ENABLED
    LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/audio-listen
    LOCAL_SRC_FILES += audio_extn/listen.c
endif

# Hardware specific feature
ifeq ($(TARGET_COMPILE_WITH_MSM_KERNEL),true)
        LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
        LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/audio
        LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/techpack/audio/include
        LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
endif

# Legacy feature
ifeq ($(strip $(AUDIO_FEATURE_SUPPORTED_EXTERNAL_BT)),true)
  LOCAL_CFLAGS += -DEXTERNAL_BT_SUPPORTED
endif

# Legacy feature
ifeq ($(strip $(AUDIO_FEATURE_FLICKER_SENSOR_INPUT)),true)
  LOCAL_CFLAGS += -DFLICKER_SENSOR_INPUT
endif

# Legacy feature
ifeq ($(strip $(AUDIO_FEATURE_NO_AUDIO_OUT)),true)
  LOCAL_CFLAGS += -DNO_AUDIO_OUT
endif

#  NonLA feature
ifeq ($(strip $(AUDIO_FEATURE_ENABLED_EXT_HDMI)),true)
    LOCAL_CFLAGS += -DAUDIO_EXTERNAL_HDMI_ENABLED
    LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/audio-parsers
    LOCAL_SHARED_LIBRARIES += libaudioparsers
endif

# Hardware specific feature
ifeq ($(strip $(BOARD_SUPPORTS_SOUND_TRIGGER)),true)
    ST_FEATURE_ENABLE := true
endif

# Hardware specific feature
ifeq ($(strip $(BOARD_SUPPORTS_SOUND_TRIGGER_HAL)),true)
    ST_FEATURE_ENABLE := true
endif

# Hardware specific feature
ifeq ($(ST_FEATURE_ENABLE), true)
    LOCAL_CFLAGS += -DSOUND_TRIGGER_ENABLED
    LOCAL_CFLAGS += -DSOUND_TRIGGER_PLATFORM_NAME=$(TARGET_BOARD_PLATFORM)
    LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/sound_trigger
    LOCAL_SRC_FILES += audio_extn/soundtrigger.c
ifneq ($(filter msm8996,$(TARGET_BOARD_PLATFORM)),)
    LOCAL_HEADER_LIBRARIES += sound_trigger.primary_headers
endif
endif

# Legacy feature
ifeq ($(strip $(AUDIO_FEATURE_ENABLED_AUXPCM_BT)),true)
    LOCAL_CFLAGS += -DAUXPCM_BT_ENABLED
endif

# Legacy feature
ifeq ($(strip $(AUDIO_FEATURE_ENABLED_PM_SUPPORT)),true)
    LOCAL_CFLAGS += -DPM_SUPPORT_ENABLED
    LOCAL_SRC_FILES += audio_extn/pm.c
    LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/libperipheralclient/inc
    LOCAL_SHARED_LIBRARIES += libperipheral_client
endif


# Hardare specific featre
ifeq ($(strip $(AUDIO_FEATURE_ENABLED_GEF_SUPPORT)),true)
    LOCAL_CFLAGS += -DAUDIO_GENERIC_EFFECT_FRAMEWORK_ENABLED
    LOCAL_SRC_FILES += audio_extn/gef.c
endif

# Hardware specific feature
ifeq ($(strip $($AUDIO_FEATURE_ADSP_HDLR_ENABLED)),true)
    LOCAL_CFLAGS += -DAUDIO_EXTN_ADSP_HDLR_ENABLED
    LOCAL_SRC_FILES += audio_extn/adsp_hdlr.c
endif


ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DYNAMIC_LOG)), true)
    LOCAL_CFLAGS += -DDYNAMIC_LOG_ENABLED
    LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/audio-log-utils
    LOCAL_SHARED_LIBRARIES += libaudio_log_utils
endif

# Hardware specific feature
ifeq ($(strip $(AUDIO_FEATURE_ENABLED_INSTANCE_ID)), true)
    LOCAL_CFLAGS += -DINSTANCE_ID_ENABLED
endif

# Kernel specific feature
ifeq ($(strip $(AUDIO_FEATURE_ENABLED_GKI)), true)
    LOCAL_CFLAGS += -DAUDIO_GKI_ENABLED
endif

# Legacy feature
ifeq ($(strip $(AUDIO_FEATURE_ENABLED_KEEP_ALIVE_ARM_FFV)), true)
    LOCAL_CFLAGS += -DRUN_KEEP_ALIVE_IN_ARM_FFV
endif

# Legacy feature
ifeq ($(strip $(AUDIO_FEATURE_ENABLED_FFV)), true)
    LOCAL_CFLAGS += -DFFV_ENABLED
    LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio-noship/include/ffv
    LOCAL_SRC_FILES += audio_extn/ffv.c
endif

# Hardware Specific feature
ifeq ($(strip $(AUDIO_FEATURE_ENABLED_AHAL_EXT)),true)
    LOCAL_CFLAGS += -DAHAL_EXT_ENABLED
    LOCAL_SHARED_LIBRARIES += vendor.qti.hardware.audiohalext@1.0
endif

LOCAL_CFLAGS += -D_GNU_SOURCE
LOCAL_CFLAGS += -Wall -Werror
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_GCOV)),true)
    LOCAL_CFLAGS += --coverage -fprofile-arcs -ftest-coverage
    LOCAL_CPPFLAGS += --coverage -fprofile-arcs -ftest-coverage
    LOCAL_STATIC_LIBRARIES += libprofile_rt
endif

LOCAL_SHARED_LIBRARIES += libbase libhidlbase libutils android.hardware.power@1.2 liblog
LOCAL_SRC_FILES += audio_perf.cpp

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_FM_TUNER_EXT)),true)
    LOCAL_CFLAGS += -DFM_TUNER_EXT_ENABLED
endif

LOCAL_MODULE := audio.primary.$(TARGET_BOARD_PLATFORM)

LOCAL_MODULE_RELATIVE_PATH := hw

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE_OWNER := qti

LOCAL_VENDOR_MODULE := true

ifneq ($(filter kona lahaina holi,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SANITIZE := integer_overflow
endif
include $(BUILD_SHARED_LIBRARY)

LOCAL_CFLAGS += -Wno-unused-variable
LOCAL_CFLAGS += -Wno-sign-compare
LOCAL_CFLAGS += -Wno-unused-parameter
LOCAL_CFLAGS += -Wno-unused-label
LOCAL_CFLAGS += -Wno-gnu-designator
LOCAL_CFLAGS += -Wno-typedef-redefinition
LOCAL_CFLAGS += -Wno-shorten-64-to-32
LOCAL_CFLAGS += -Wno-tautological-compare
LOCAL_CFLAGS += -Wno-unused-function
LOCAL_CFLAGS += -Wno-unused-local-typedef

endif
endif
