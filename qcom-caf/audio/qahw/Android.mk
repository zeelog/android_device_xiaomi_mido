ifneq ($(AUDIO_USE_STUB_HAL), true)
ifeq ($(strip $(BOARD_SUPPORTS_QAHW)),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

libqahw-inc := $(LOCAL_PATH)/inc

LOCAL_MODULE := libqahwwrapper
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES   := $(libqahw-inc)

LOCAL_HEADER_LIBRARIES := libutils_headers \
    libsystem_headers \
    libhardware_headers

LOCAL_SRC_FILES := \
    src/qahw.c \
    src/qahw_effect.c

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libcutils \
    libhardware \
    libdl

LOCAL_CFLAGS += -Wall -Werror
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)
LOCAL_PROPRIETARY_MODULE := true
LOCAL_VENDOR_MODULE     := true

ifneq ($(filter kona lahaina holi,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SANITIZE := integer_overflow
endif
include $(BUILD_SHARED_LIBRARY)

endif
endif
