LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    FmIoctlsInterface.cpp \
    ConfigFmThs.cpp \
    FmPerformanceParams.cpp \
    ConfFileParser.cpp \
    FmRadioController.cpp \
    LibfmJni.cpp

LOCAL_C_INCLUDES := \
    frameworks/base/core/jni/include \
    frameworks/base/include/media

LOCAL_HEADER_LIBRARIES := \
    jni_headers

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libdl \
    liblog \
    libmedia \
    libnativehelper \

ifeq ($(TARGET_QCOM_NO_FM_FIRMWARE),true)
    LOCAL_CFLAGS += -DQCOM_NO_FM_FIRMWARE
endif

LOCAL_MODULE := libqcomfmjni
include $(BUILD_SHARED_LIBRARY)
