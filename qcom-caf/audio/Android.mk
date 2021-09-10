ifeq ($(call my-dir),$(call project-path-for,qcom-audio))

ifneq ($(AUDIO_USE_STUB_HAL), true)
ifneq ($(filter mpq8092 msm8960 msm8226 msm8x26 msm8610 msm8974 msm8x74 apq8084 msm8916 msm8994 msm8992 msm8909 msm8996 msm8952 msm8937 thorium msm8953 msmgold msm8998 sdm660 sdm845 sdm710 apq8098_latv qcs605 sdmshrike msmnile kona lahaina holi $(MSMSTEPPE) $(TRINKET) atoll lito bengal,$(TARGET_BOARD_PLATFORM)),)

MY_LOCAL_PATH := $(call my-dir)

ifeq ($(BOARD_USES_LEGACY_ALSA_AUDIO),true)
include $(MY_LOCAL_PATH)/legacy/Android.mk
else
ifneq ($(filter mpq8092,$(TARGET_BOARD_PLATFORM)),)
include $(MY_LOCAL_PATH)/hal_mpq/Android.mk
else
include $(MY_LOCAL_PATH)/hal/Android.mk
endif
include $(MY_LOCAL_PATH)/hal/audio_extn/Android.mk
include $(MY_LOCAL_PATH)/voice_processing/Android.mk
include $(MY_LOCAL_PATH)/mm-audio/Android.mk
include $(MY_LOCAL_PATH)/visualizer/Android.mk
include $(MY_LOCAL_PATH)/post_proc/Android.mk
include $(MY_LOCAL_PATH)/qahw/Android.mk
include $(MY_LOCAL_PATH)/qahw_api/Android.mk
endif

ifeq ($(USE_LEGACY_AUDIO_DAEMON), true)
include $(MY_LOCAL_PATH)/audiod/Android.mk
endif

endif
endif
endif
