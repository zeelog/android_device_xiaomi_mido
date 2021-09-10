ifeq ($(call my-dir),$(call project-path-for,qcom-media))

QCOM_MEDIA_ROOT := $(call my-dir)

#Compile these for all targets under QCOM_BOARD_PLATFORMS list.
ifeq ($(call is-board-platform-in-list, $(QCOM_BOARD_PLATFORMS)),true)
include $(QCOM_MEDIA_ROOT)/mm-core/Android.mk
include $(QCOM_MEDIA_ROOT)/libstagefrighthw/Android.mk
include $(QCOM_MEDIA_ROOT)/libaac/Android.mk
endif

ifeq ($(call is-board-platform-in-list, $(MSM_VIDC_TARGET_LIST)),true)
include $(QCOM_MEDIA_ROOT)/mm-video-v4l2/Android.mk

ifeq ($(BOARD_USES_ADRENO), true)
include $(QCOM_MEDIA_ROOT)/libc2dcolorconvert/Android.mk
endif

include $(QCOM_MEDIA_ROOT)/hypv-intercept/Android.mk

ifeq ($(TARGET_BOARD_AUTO),true)
include $(QCOM_MEDIA_ROOT)/libsidebandstreamhandle/Android.mk
endif

endif

endif
