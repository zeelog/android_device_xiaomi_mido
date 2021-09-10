ifneq ($(BUILD_TINY_ANDROID),true)

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

OMXCORE_CFLAGS := -g -O3 -DVERBOSE
OMXCORE_CFLAGS += -O0 -fno-inline -fno-short-enums
OMXCORE_CFLAGS += -D_ANDROID_
OMXCORE_CFLAGS += -U_ENABLE_QC_MSG_LOG_

#===============================================================================
#             Figure out the targets
#===============================================================================

ifeq ($(TARGET_BOARD_PLATFORM),msm7627a)
MM_CORE_TARGET = 7627A
else ifeq ($(TARGET_BOARD_PLATFORM),msm7630_surf)
MM_CORE_TARGET = 7630
else ifeq ($(TARGET_BOARD_PLATFORM),msm8660)
MM_CORE_TARGET = 8660
#Comment out following line to disable drm.play component
OMXCORE_CFLAGS += -DENABLE_DRMPLAY
else ifeq ($(TARGET_BOARD_PLATFORM),msm8960)
MM_CORE_TARGET = 8960
else ifeq ($(TARGET_BOARD_PLATFORM),msm8974)
MM_CORE_TARGET = 8974
else ifeq ($(TARGET_BOARD_PLATFORM),msm8610)
MM_CORE_TARGET = 8610
else ifeq ($(TARGET_BOARD_PLATFORM),msm8226)
MM_CORE_TARGET = 8226
else ifeq ($(TARGET_BOARD_PLATFORM),msm8916)
MM_CORE_TARGET = 8916
else ifeq ($(TARGET_BOARD_PLATFORM),msm8909)
MM_CORE_TARGET = 8909
else ifeq ($(TARGET_BOARD_PLATFORM),msm8937)
MM_CORE_TARGET = 8937
else ifeq ($(TARGET_BOARD_PLATFORM),apq8084)
MM_CORE_TARGET = 8084
else ifeq ($(TARGET_BOARD_PLATFORM),mpq8092)
MM_CORE_TARGET = 8092
else ifeq ($(TARGET_BOARD_PLATFORM),msm8992)
MM_CORE_TARGET = msm8992
else ifeq ($(TARGET_BOARD_PLATFORM),msm8994)
MM_CORE_TARGET = msm8994
else ifeq ($(TARGET_BOARD_PLATFORM),msm8996)
MM_CORE_TARGET = msm8996
else ifeq ($(TARGET_BOARD_PLATFORM),msm8952)
MM_CORE_TARGET = 8952
else ifeq ($(TARGET_BOARD_PLATFORM),msm8953)
MM_CORE_TARGET = msm8953
else ifeq ($(TARGET_BOARD_PLATFORM),msm8998)
MM_CORE_TARGET = msm8998
else ifeq ($(TARGET_BOARD_PLATFORM),apq8098_latv)
MM_CORE_TARGET = msm8998
else ifeq ($(TARGET_BOARD_PLATFORM),sdm660)
MM_CORE_TARGET = sdm660
else
MM_CORE_TARGET = default
endif

ifeq ($(call is-platform-sdk-version-at-least,27),true) # O-MR1
OMXCORE_CFLAGS += -D_ANDROID_O_MR1_DIVX_CHANGES
endif

#===============================================================================
#             LIBRARY for Android apps
#===============================================================================

LOCAL_C_INCLUDES        := $(LOCAL_PATH)/src/common

LOCAL_HEADER_LIBRARIES := \
        libutils_headers \
        libomxcore_headers

LOCAL_EXPORT_HEADER_LIBRARY_HEADERS := libomxcore_headers

LOCAL_PRELINK_MODULE    := false
LOCAL_MODULE            := libOmxCore
LOCAL_MODULE_TAGS       := optional
LOCAL_VENDOR_MODULE     := true
LOCAL_SHARED_LIBRARIES  := liblog libdl libcutils
LOCAL_CFLAGS            := $(OMXCORE_CFLAGS)

LOCAL_SRC_FILES         := src/common/omx_core_cmp.cpp
LOCAL_SRC_FILES         += src/common/qc_omx_core.c
ifneq (,$(filter msm8916 msm8994 msm8909 msm8937 msm8996 msm8992 msm8952 msm8953 msm8998 apq8098_latv sdm660,$(TARGET_BOARD_PLATFORM)))
LOCAL_SRC_FILES         += src/$(MM_CORE_TARGET)/registry_table_android.c
else
LOCAL_SRC_FILES         += src/$(MM_CORE_TARGET)/qc_registry_table_android.c
endif

include $(BUILD_SHARED_LIBRARY)

#===============================================================================
#             LIBRARY for command line test apps
#===============================================================================

include $(CLEAR_VARS)

LOCAL_C_INCLUDES        := $(LOCAL_PATH)/src/common

LOCAL_HEADER_LIBRARIES := \
        libutils_headers \
        libomxcore_headers

LOCAL_EXPORT_HEADER_LIBRARY_HEADERS := libomxcore_headers

LOCAL_PRELINK_MODULE    := false
LOCAL_MODULE            := libmm-omxcore
LOCAL_MODULE_TAGS       := optional
LOCAL_VENDOR_MODULE     := true
LOCAL_SHARED_LIBRARIES  := liblog libdl libcutils
LOCAL_CFLAGS            := $(OMXCORE_CFLAGS)

LOCAL_SRC_FILES         := src/common/omx_core_cmp.cpp
LOCAL_SRC_FILES         += src/common/qc_omx_core.c
ifneq (,$(filter msm8916 msm8994 msm8909 msm8937 msm8996 msm8992 msm8952 msm8953 msm8998 apq8098_latv sdm660,$(TARGET_BOARD_PLATFORM)))
LOCAL_SRC_FILES         += src/$(MM_CORE_TARGET)/registry_table.c
else
LOCAL_SRC_FILES         += src/$(MM_CORE_TARGET)/qc_registry_table.c
endif

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libomxcore_headers
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)/inc
LOCAL_VENDOR_MODULE := true

include $(BUILD_HEADER_LIBRARY)

endif #BUILD_TINY_ANDROID
