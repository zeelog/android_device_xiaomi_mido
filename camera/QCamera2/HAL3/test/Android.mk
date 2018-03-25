LOCAL_PATH:=$(call my-dir)

# Build command line test app: mm-hal3-app
include $(CLEAR_VARS)

ifeq ($(TARGET_SUPPORT_HAL1),false)
LOCAL_CFLAGS += -DQCAMERA_HAL3_SUPPORT
endif

ifeq ($(TARGET_BOARD_PLATFORM),msm8953)
    LOCAL_CFLAGS += -DCAMERA_CHIPSET_8953
else
    LOCAL_CFLAGS += -DCAMERA_CHIPSET_8937
endif

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_C_INCLUDES+= $(kernel_includes)

LOCAL_C_INCLUDES += \
    hardware/libhardware/include/hardware \
    system/media/camera/include \
    system/media/private/camera/include \
    $(LOCAL_PATH)/../ \
    $(LOCAL_PATH)/../../stack/mm-camera-interface/inc \


LOCAL_SRC_FILES := \
    QCameraHAL3Base.cpp \
    QCameraHAL3MainTestContext.cpp \
    QCameraHAL3VideoTest.cpp \
    QCameraHAL3PreviewTest.cpp \
    QCameraHAL3SnapshotTest.cpp \
    QCameraHAL3RawSnapshotTest.cpp \
    QCameraHAL3Test.cpp


LOCAL_SHARED_LIBRARIES:= libutils libcamera_client liblog libcamera_metadata libcutils

LOCAL_32_BIT_ONLY := $(BOARD_QTI_CAMERA_32BIT_ONLY)

LOCAL_MODULE:= hal3-test-app
LOCAL_VENDOR_MODULE := true

LOCAL_CFLAGS += -Wall -Wextra

LOCAL_CFLAGS += -std=c++11 -std=gnu++0x

include $(BUILD_EXECUTABLE)
