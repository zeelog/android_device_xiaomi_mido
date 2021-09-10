ifneq ($(AUDIO_USE_STUB_HAL), true)
LOCAL_PATH:= $(call my-dir)

# audio preprocessing wrapper
include $(CLEAR_VARS)

LOCAL_CFLAGS += \
    -Wall \
    -Werror \
    -Wno-unused-variable \
    -Wno-gnu-designator \
    -Wno-unused-value \
    -Wno-unused-function

LOCAL_MODULE:= libqcomvoiceprocessing
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_RELATIVE_PATH := soundfx
LOCAL_VENDOR_MODULE := true
LOCAL_MODULE_OWNER := qti

LOCAL_SRC_FILES:= \
    voice_processing.c

LOCAL_C_INCLUDES += \
    $(call include-path-for, audio-effects)

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libcutils

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_GCOV)),true)
LOCAL_CFLAGS += --coverage -fprofile-arcs -ftest-coverage
LOCAL_CPPFLAGS += --coverage -fprofile-arcs -ftest-coverage
LOCAL_STATIC_LIBRARIES += libprofile_rt
endif

LOCAL_SHARED_LIBRARIES += libdl

LOCAL_HEADER_LIBRARIES := libhardware_headers
LOCAL_CFLAGS += -fvisibility=hidden

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

ifneq ($(filter kona lahaina holi,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SANITIZE := integer_overflow
endif
include $(BUILD_SHARED_LIBRARY)
endif
