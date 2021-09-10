ifneq ($(BUILD_TINY_ANDROID),true)

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

# ---------------------------------------------------------------------------------
#                 Common definitons
# ---------------------------------------------------------------------------------

libOmxAacEnc-def := -g -O3
libOmxAacEnc-def += -DQC_MODIFIED
libOmxAacEnc-def += -D_ANDROID_
libOmxAacEnc-def += -D_ENABLE_QC_MSG_LOG_
libOmxAacEnc-def += -DVERBOSE
libOmxAacEnc-def += -D_DEBUG
libOmxAacEnc-def += -Wconversion
libOmxAacEnc-def += -DAUDIOV2

libOmxAacEnc-def += -Wno-sign-conversion -Wno-shorten-64-to-32 -Wno-self-assign -Wno-parentheses-equality -Wno-format -Wno-sign-compare -Wno-tautological-compare -Wno-shorten-64-to-32 -Wno-unused-local-typedef
# ---------------------------------------------------------------------------------
#             Make the Shared library (libOmxAacEnc)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)

libOmxAacEnc-inc       := $(LOCAL_PATH)/inc

LOCAL_MODULE             := libOmxAacEnc
LOCAL_MODULE_TAGS        := optional
LOCAL_VENDOR_MODULE      := true
LOCAL_CFLAGS            := $(libOmxAacEnc-def)
LOCAL_CFLAGS            := -Wno-format -Wno-sign-compare -Wno-sign-conversion -Wno-self-assign -Wno-parentheses-equality
LOCAL_C_INCLUDES        := $(libOmxAacEnc-inc)
LOCAL_PRELINK_MODULE    := false
LOCAL_SHARED_LIBRARIES  := libutils liblog
LOCAL_SRC_FILES         := src/aenc_svr.c
LOCAL_SRC_FILES         += src/omx_aac_aenc.cpp

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_GCOV)),true)
LOCAL_CFLAGS += --coverage -fprofile-arcs -ftest-coverage
LOCAL_CPPFLAGS += --coverage -fprofile-arcs -ftest-coverage
LOCAL_STATIC_LIBRARIES += libprofile_rt
endif

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include/audio
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/techpack/audio/include
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

LOCAL_HEADER_LIBRARIES := libomxcore_headers
ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DLKM)),true)
  LOCAL_HEADER_LIBRARIES += audio_kernel_headers
  LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/vendor/qcom/opensource/audio-kernel/include
endif

ifneq ($(filter kona lahaina holi,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SANITIZE := integer_overflow
endif
include $(BUILD_SHARED_LIBRARY)

# ---------------------------------------------------------------------------------
#             Make the apps-test (mm-aenc-omxaac-test)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)

mm-aac-enc-test-inc    := $(LOCAL_PATH)/inc
mm-aac-enc-test-inc    += $(LOCAL_PATH)/test

LOCAL_MODULE            := mm-aenc-omxaac-test
LOCAL_MODULE_TAGS       := optional
LOCAL_CFLAGS            := $(libOmxAacEnc-def)
LOCAL_CFLAGS            := -Wno-unused-local-typedef -Wno-shorten-64-to-32
LOCAL_C_INCLUDES        := $(mm-aac-enc-test-inc)
LOCAL_PRELINK_MODULE    := false
LOCAL_SHARED_LIBRARIES  := libmm-omxcore
LOCAL_SHARED_LIBRARIES  += libOmxAacEnc
LOCAL_VENDOR_MODULE     := true
LOCAL_SRC_FILES         := test/omx_aac_enc_test.c

ifneq ($(filter kona lahaina holi,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SANITIZE := integer_overflow
endif
include $(BUILD_EXECUTABLE)

endif

# ---------------------------------------------------------------------------------
#                     END
# ---------------------------------------------------------------------------------

