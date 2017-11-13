LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SHARED_LIBRARIES := \
    libandroid \
    libcutils \
    liblog

LOCAL_SRC_FILES := \
    main.cpp

LOCAL_C_INCLUDES :=

LOCAL_CFLAGS := $(common_flags) -DLOG_TAG=\"folio_daemon\" -DLOG_NDEBUG=0

LOCAL_CFLAGS += -Wall -Werror

LOCAL_MODULE := folio_daemon
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_OWNER := google
LOCAL_PROPRIETARY_MODULE := true

include $(BUILD_EXECUTABLE)
