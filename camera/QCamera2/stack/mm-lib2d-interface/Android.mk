OLD_LOCAL_PATH := $(LOCAL_PATH)
LOCAL_PATH := $(call my-dir)

include $(LOCAL_PATH)/../../../common.mk
include $(CLEAR_VARS)

LOCAL_32_BIT_ONLY := $(BOARD_QTI_CAMERA_32BIT_ONLY)
LOCAL_CFLAGS+= -D_ANDROID_ -DQCAMERA_REDEFINE_LOG

LOCAL_CFLAGS += -Wall -Wextra -Werror -Wno-unused-parameter

LOCAL_C_INCLUDES+= $(kernel_includes)
LOCAL_ADDITIONAL_DEPENDENCIES := $(common_deps)

IMGLIB_HEADER_PATH := $(TARGET_OUT_INTERMEDIATES)/include/mm-camera/imglib

LOCAL_C_INCLUDES += \
    $(IMGLIB_HEADER_PATH) \
    $(LOCAL_PATH)/inc \
    $(LOCAL_PATH)/../common \
    $(LOCAL_PATH)/../mm-camera-interface/inc \

ifeq ($(strip $(TARGET_USES_ION)),true)
    LOCAL_CFLAGS += -DUSE_ION
endif


LOCAL_SRC_FILES := \
    src/mm_lib2d.c

LOCAL_MODULE           := libmmlib2d_interface
LOCAL_SHARED_LIBRARIES := libdl libcutils liblog libmmcamera_interface
LOCAL_MODULE_TAGS := optional
LOCAL_VENDOR_MODULE := true

LOCAL_32_BIT_ONLY := $(BOARD_QTI_CAMERA_32BIT_ONLY)
include $(BUILD_SHARED_LIBRARY)

LOCAL_PATH := $(OLD_LOCAL_PATH)
