
##AUDIO_FEATURE_FLAGS

BOARD_USES_ALSA_AUDIO := true

#TODO move this cchange to device/qcom/msm8909

ifneq ($(TARGET_USES_AOSP_FOR_AUDIO), true)
USE_CUSTOM_AUDIO_POLICY := 1
AUDIO_FEATURE_ENABLED_COMPRESS_VOIP := true
AUDIO_FEATURE_ENABLED_EXTN_FORMATS := true
AUDIO_FEATURE_ENABLED_EXTN_FLAC_DECODER := true
AUDIO_FEATURE_ENABLED_EXTN_RESAMPLER := true
AUDIO_FEATURE_ENABLED_FM_POWER_OPT := true
AUDIO_FEATURE_ENABLED_PROXY_DEVICE := true
#TODO Enable SSR
#AUDIO_FEATURE_ENABLED_SSR := true
AUDIO_FEATURE_ENABLED_VOICE_CONCURRENCY := true
AUDIO_FEATURE_ENABLED_RECORD_PLAY_CONCURRENCY := true
#TODO Enable PM
#AUDIO_FEATURE_ENABLED_PM_SUPPORT := true
AUDIO_FEATURE_ENABLED_DS2_DOLBY_DAP := false
MM_AUDIO_ENABLED_SAFX := true
DOLBY_ENABLE := false

endif
USE_XML_AUDIO_POLICY_CONF := 1
BOARD_SUPPORTS_SOUND_TRIGGER := true
BOARD_SUPPORTS_SOUND_TRIGGER_ARM := true
AUDIO_FEATURE_ENABLED_NT_PAUSE_TIMEOUT := true
AUDIO_FEATURE_ENABLED_FFV := true
AUDIO_FEATURE_ENABLED_KEEP_ALIVE_ARM_FFV := true
AUDIO_FEATURE_ENABLED_KEEP_ALIVE := true
AUDIO_FEATURE_ENABLED_SOURCE_TRACKING := true
AUDIO_FEATURE_ENABLED_FLUENCE := true
AUDIO_FEATURE_ENABLED_HFP := true
AUDIO_FEATURE_ENABLED_MULTI_VOICE_SESSIONS := true
AUDIO_FEATURE_ENABLED_KPI_OPTIMIZE := true
AUDIO_FEATURE_ENABLED_ACDB_LICENSE := true
AUDIO_FEATURE_ENABLED_DYNAMIC_LOG := true
MM_AUDIO_ENABLED_FTM := true
TARGET_USES_QCOM_MM_AUDIO := true
AUDIO_FEATURE_ENABLED_SND_MONITOR := true
BOARD_SUPPORTS_QAHW := true
BOARD_SUPPORTS_QSTHW_API := true
AUDIO_FEATURE_DISABLED_SOUND_TRIGGER_LEGACY_HAL := true
AUDIO_FEATURE_ENABLED_COMPRESS_INPUT := true
AUDIO_FEATURE_ENABLED_CUSTOMSTEREO := true
COMPILE_HAL_TEST_APPS_IN_VENDOR_IMAGE := true

##AUDIO_FEATURE_FLAGS

#Audio Specific device overlays
DEVICE_PACKAGE_OVERLAYS += vendor/qcom/opensource/audio-hal/primary-hal/configs/common/overlay


# Audio configuration file
ifeq ($(TARGET_USES_AOSP_FOR_AUDIO), true)
PRODUCT_COPY_FILES += \
    device/qcom/common/media/audio_policy.conf:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy.conf
else
PRODUCT_COPY_FILES += \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8909/audio_policy.conf:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy.conf
endif
PRODUCT_COPY_FILES += \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8909/audio_effects.conf:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects.conf \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8909/audio_effects.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8909/mixer_paths_qrd_skuh.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths_qrd_skuh.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8909/mixer_paths_qrd_skui.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths_qrd_skui.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8909/mixer_paths.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8909/mixer_paths_msm8909_pm8916.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths_msm8909_pm8916.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8909/mixer_paths_wcd9326_i2s.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths_wcd9326_i2s.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8909/mixer_paths_wcd9326_i2s_tdm.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths_wcd9326_i2s_tdm.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8909/mixer_paths_skua.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths_skua.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8909/mixer_paths_skuc.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths_skuc.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8909/mixer_paths_skue.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths_skue.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8909/mixer_paths_qrd_skut.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths_qrd_skut.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8909/sound_trigger_mixer_paths.xml:$(TARGET_COPY_OUT_VENDOR)/etc/sound_trigger_mixer_paths.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8909/sound_trigger_mixer_paths_wcd9326.xml:$(TARGET_COPY_OUT_VENDOR)/etc/sound_trigger_mixer_paths_wcd9326.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8909/sound_trigger_platform_info.xml:$(TARGET_COPY_OUT_VENDOR)/etc/sound_trigger_platform_info.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8909/sound_trigger_mixer_paths_wcd9335.xml:$(TARGET_COPY_OUT_VENDOR)/etc/sound_trigger_mixer_paths_wcd9335.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8909/audio_platform_info.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_platform_info.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8909/audio_platform_info_extcodec.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_platform_info_extcodec.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8909/audio_io_policy.conf:$(TARGET_COPY_OUT_VENDOR)/etc/audio_io_policy.conf

#XML Audio configuration files
ifeq ($(USE_XML_AUDIO_POLICY_CONF), 1)
ifneq ($(TARGET_USES_AOSP_FOR_AUDIO), true)
PRODUCT_COPY_FILES += \
    $(TOPDIR)vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8909/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio/audio_policy_configuration.xml
endif
PRODUCT_COPY_FILES += \
    $(TOPDIR)vendor/qcom/opensource/audio-hal/primary-hal/configs/common/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    $(TOPDIR)vendor/qcom/opensource/audio-hal/primary-hal/configs/common/bluetooth_qti_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/a2dp_audio_policy_configuration.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/usb_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/usb_audio_policy_configuration.xml
endif


PRODUCT_PROPERTY_OVERRIDES += \
ro.vendor.audio.sdk.ssr=false

PRODUCT_PROPERTY_OVERRIDES += \
ro.vendor.audio.sdk.ffv=false

##fluencetype can be "fluence" or "fluencepro" or "fluenceffv" or "none"
PRODUCT_PROPERTY_OVERRIDES += \
ro.vendor.audio.sdk.fluencetype=none\
persist.vendor.audio.fluence.voicecall=true\
persist.vendor.audio.fluence.voicerec=false\
persist.vendor.audio.fluence.speaker=true\
persist.vendor.audio.fluence.audiorec=false

#enable generic handset mic
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.apptype.multirec.enabled=false

#enable multi record
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.record.multiple.enabled=true

#disable tunnel encoding
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.tunnel.encode=false

#Buffer size in kbytes for compress offload playback
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.offload.buffer.size.kb=64

#Minimum duration for offload playback in secs
PRODUCT_PROPERTY_OVERRIDES += \
audio.offload.min.duration.secs=30

#Enable offload audio video playback by default
PRODUCT_PROPERTY_OVERRIDES += \
audio.offload.video=true

#enable voice path for PCM VoIP by default
PRODUCT_PROPERTY_OVERRIDES += \
vendor.voice.path.for.pcm.voip=true

#enable dsp gapless mode by default
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.offload.gapless.enabled=true

#Audio voice concurrency related flags
PRODUCT_PROPERTY_OVERRIDES += \
vendor.voice.playback.conc.disabled=true\
vendor.voice.record.conc.disabled=true\
vendor.voice.voip.conc.disabled=true

#Audio VoIP / playback record concurrency flags
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.rec.playback.conc.disabled=true

#property to enable image unload by audio HAL
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.sys.init=false

#Enable DS2 feature for Dolby
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.dolby.ds2.enabled=true

PRODUCT_PROPERTY_OVERRIDES += \
persist.vendor.audio.hw.binder.size_kbyte=1024

#Disable split a2dp
PRODUCT_PROPERTY_OVERRIDES += \
persist.vendor.bt.enable.splita2dp=false

#enable headset calibration
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.volume.headset.gain.depcal=true

PRODUCT_PACKAGES += \
    android.hardware.audio@2.0-service \
    android.hardware.audio@2.0-impl \
    android.hardware.audio.effect@2.0-impl \
    android.hardware.soundtrigger@2.1-impl \
    android.hardware.audio@4.0 \
    android.hardware.audio.common@4.0 \
    android.hardware.audio.common@4.0-util \
    android.hardware.audio@4.0-impl \
    android.hardware.audio.effect@4.0 \
    android.hardware.audio.effect@4.0-impl
