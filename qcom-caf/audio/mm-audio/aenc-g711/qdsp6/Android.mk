ifneq ($(BUILD_TINY_ANDROID),true)

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

# ---------------------------------------------------------------------------------
#                 Common definitons
# ---------------------------------------------------------------------------------

libOmxG711Enc-def := -g -O3
libOmxG711Enc-def += -DQC_MODIFIED
libOmxG711Enc-def += -D_ANDROID_
libOmxG711Enc-def += -D_ENABLE_QC_MSG_LOG_
libOmxG711Enc-def += -DVERBOSE
libOmxG711Enc-def += -D_DEBUG
libOmxG711Enc-def += -Wconversion
libOmxG711Enc-def += -DAUDIOV2

libOmxG711Enc-def += -Wno-sign-conversion -Wno-shorten-64-to-32 -Wno-self-assign -Wno-parentheses-equality -Wno-format -Wno-sign-compare -Wno-tautological-compare -Wno-shorten-64-to-32 -Wno-unused-local-typedef

# ---------------------------------------------------------------------------------
#             Make the Shared library (libOmxG711Enc)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)

libOmxG711Enc-inc       := $(LOCAL_PATH)/inc

LOCAL_MODULE             := libOmxG711Enc
LOCAL_MODULE_TAGS        := optional
LOCAL_VENDOR_MODULE      := true
LOCAL_CFLAGS            := $(libOmxG711Enc-def)
LOCAL_CFLAGS            := -Wno-format -Wno-sign-compare -Wno-sign-conversion -Wno-self-assign -Wno-parentheses-equality
LOCAL_C_INCLUDES        := $(libOmxG711Enc-inc)
LOCAL_PRELINK_MODULE    := false
LOCAL_SHARED_LIBRARIES  := libutils liblog libcutils

LOCAL_SRC_FILES         := src/aenc_svr.c
LOCAL_SRC_FILES         += src/omx_g711_aenc.cpp
LOCAL_SRC_FILES         += src/omx_log.cpp

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
#             Make the apps-test (mm-aenc-omxg711-test)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)

mm-g711-enc-test-inc   := $(LOCAL_PATH)/inc
mm-g711-enc-test-inc   += $(LOCAL_PATH)/test

LOCAL_MODULE            := mm-aenc-omxg711-test
LOCAL_MODULE_TAGS       := optional
LOCAL_CFLAGS            := $(libOmxG711Enc-def)
LOCAL_CFLAGS            := -Wno-tautological-compare -Wno-unused-local-typedef
LOCAL_C_INCLUDES        := $(mm-g711-enc-test-inc)
LOCAL_PRELINK_MODULE    := false
LOCAL_SHARED_LIBRARIES  := libmm-omxcore
LOCAL_SHARED_LIBRARIES  += libOmxG711Enc
LOCAL_VENDOR_MODULE     := true
LOCAL_SRC_FILES         := test/omx_g711_enc_test.c

ifneq ($(filter kona lahaina holi,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SANITIZE := integer_overflow
endif
include $(BUILD_EXECUTABLE)

endif

# ---------------------------------------------------------------------------------
#                     END
# ---------------------------------------------------------------------------------

