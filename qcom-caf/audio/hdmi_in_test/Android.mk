LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := hdmi_in_test
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_OWNER := qti

LOCAL_SRC_FILES := \
    src/hdmi_in_event_test.c

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libcutils

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_GCOV)),true)
LOCAL_CFLAGS += --coverage -fprofile-arcs -ftest-coverage
LOCAL_CPPFLAGS += --coverage -fprofile-arcs -ftest-coverage
LOCAL_STATIC_LIBRARIES += libprofile_rt
endif

ifneq ($(filter kona lahaina holi,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SANITIZE := integer_overflow
endif
include $(BUILD_EXECUTABLE)
