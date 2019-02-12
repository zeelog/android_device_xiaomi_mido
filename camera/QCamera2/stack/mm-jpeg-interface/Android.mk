OLD_LOCAL_PATH := $(LOCAL_PATH)
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_32_BIT_ONLY := $(BOARD_QTI_CAMERA_32BIT_ONLY)
LOCAL_CFLAGS+= -D_ANDROID_ -DQCAMERA_REDEFINE_LOG

LOCAL_CFLAGS += -Wall -Wextra -Werror -Wno-unused-parameter

LOCAL_HEADER_LIBRARIES := generated_kernel_headers

LIB2D_ROTATION=false

LOCAL_C_INCLUDES += \
    frameworks/native/include/media/openmax \
    $(LOCAL_PATH)/inc \
    $(LOCAL_PATH)/../common \
    $(LOCAL_PATH)/../mm-camera-interface/inc \
    $(LOCAL_PATH)/../../.. \
    $(LOCAL_PATH)/../../../mm-image-codec/qexif \
    $(LOCAL_PATH)/../../../mm-image-codec/qomx_core

ifeq ($(strip $(LIB2D_ROTATION)),true)
    LOCAL_C_INCLUDES += $(LOCAL_PATH)/../mm-lib2d-interface/inc
    LOCAL_CFLAGS += -DLIB2D_ROTATION_ENABLE
endif


ifeq ($(strip $(TARGET_USES_ION)),true)
    LOCAL_CFLAGS += -DUSE_ION
endif

ifneq (,$(filter  msm8610,$(TARGET_BOARD_PLATFORM)))
    LOCAL_CFLAGS+= -DLOAD_ADSP_RPC_LIB
endif

DUAL_JPEG_TARGET_LIST := msm8974
DUAL_JPEG_TARGET_LIST += msm8994

ifneq (,$(filter  $(DUAL_JPEG_TARGET_LIST),$(TARGET_BOARD_PLATFORM)))
    LOCAL_CFLAGS+= -DMM_JPEG_CONCURRENT_SESSIONS_COUNT=2
else
    LOCAL_CFLAGS+= -DMM_JPEG_CONCURRENT_SESSIONS_COUNT=1
endif

JPEG_PIPELINE_TARGET_LIST := msm8994
JPEG_PIPELINE_TARGET_LIST += msm8992
JPEG_PIPELINE_TARGET_LIST += msm8996
JPEG_PIPELINE_TARGET_LIST += msmcobalt

ifneq (,$(filter  $(JPEG_PIPELINE_TARGET_LIST),$(TARGET_BOARD_PLATFORM)))
    LOCAL_CFLAGS+= -DMM_JPEG_USE_PIPELINE
endif

# System header file path prefix
LOCAL_CFLAGS += -DSYSTEM_HEADER_PREFIX=sys

LOCAL_SRC_FILES := \
    src/mm_jpeg_queue.c \
    src/mm_jpeg_exif.c \
    src/mm_jpeg.c \
    src/mm_jpeg_interface.c \
    src/mm_jpeg_ionbuf.c \
    src/mm_jpegdec_interface.c \
    src/mm_jpegdec.c \
    src/mm_jpeg_mpo_composer.c

LOCAL_MODULE           := libmmjpeg_interface
LOCAL_SHARED_LIBRARIES := libdl libcutils liblog libqomx_core libmmcamera_interface libutils
ifeq ($(strip $(LIB2D_ROTATION)),true)
    LOCAL_SHARED_LIBRARIES += libmmlib2d_interface
endif
LOCAL_MODULE_TAGS := optional
LOCAL_VENDOR_MODULE := true

LOCAL_32_BIT_ONLY := $(BOARD_QTI_CAMERA_32BIT_ONLY)
include $(BUILD_SHARED_LIBRARY)

LOCAL_PATH := $(OLD_LOCAL_PATH)
