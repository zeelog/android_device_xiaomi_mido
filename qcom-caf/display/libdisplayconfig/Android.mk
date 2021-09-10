LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE                  := libdisplayconfig
LOCAL_MODULE_TAGS             := optional
LOCAL_C_INCLUDES              := $(common_includes)
LOCAL_HEADER_LIBRARIES        := display_headers
LOCAL_SRC_FILES               := DisplayConfig.cpp
LOCAL_SHARED_LIBRARIES        := libhidlbase libutils vendor.display.config@1.0
LOCAL_EXPORT_C_INCLUDE_DIRS   := $(LOCAL_PATH)

include $(BUILD_SHARED_LIBRARY)
