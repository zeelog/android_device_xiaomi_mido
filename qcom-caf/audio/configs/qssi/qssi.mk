#BOARD_USES_GENERIC_AUDIO := true
#
#AUDIO_FEATURE_FLAGS
BOARD_USES_ALSA_AUDIO := true
TARGET_USES_AOSP_FOR_AUDIO := false
AUDIO_FEATURE_QSSI_COMPLIANCE := true

ifneq ($(TARGET_USES_AOSP_FOR_AUDIO), true)
ifeq ($(TARGET_FWK_SUPPORTS_FULL_VALUEADDS),true)
USE_CUSTOM_AUDIO_POLICY := 1
else
USE_CUSTOM_AUDIO_POLICY := 0
endif
AUDIO_FEATURE_ENABLED_COMPRESS_VOIP := false
AUDIO_FEATURE_ENABLED_EXTN_FORMATS := true
AUDIO_FEATURE_ENABLED_EXTN_FLAC_DECODER := true
AUDIO_FEATURE_ENABLED_EXTN_RESAMPLER := true
AUDIO_FEATURE_ENABLED_FM_POWER_OPT := true
AUDIO_FEATURE_ENABLED_HDMI_SPK := true
AUDIO_FEATURE_ENABLED_PCM_OFFLOAD := true
AUDIO_FEATURE_ENABLED_PCM_OFFLOAD_24 := true
AUDIO_FEATURE_ENABLED_FLAC_OFFLOAD := true
AUDIO_FEATURE_ENABLED_VORBIS_OFFLOAD := true
AUDIO_FEATURE_ENABLED_WMA_OFFLOAD := true
AUDIO_FEATURE_ENABLED_ALAC_OFFLOAD := true
AUDIO_FEATURE_ENABLED_APE_OFFLOAD := true
AUDIO_FEATURE_ENABLED_AAC_ADTS_OFFLOAD := true
AUDIO_FEATURE_ENABLED_PROXY_DEVICE := true
AUDIO_FEATURE_ENABLED_AUDIOSPHERE := true
AUDIO_FEATURE_ENABLED_3D_AUDIO := true
AUDIO_FEATURE_ENABLED_AHAL_EXT := true
DOLBY_ENABLE := false
endif

USE_XML_AUDIO_POLICY_CONF := 1
BOARD_SUPPORTS_SOUND_TRIGGER := true
#AUDIO_FEATURE_ENABLED_KEEP_ALIVE := true
AUDIO_FEATURE_ENABLED_DS2_DOLBY_DAP := false
TARGET_USES_QCOM_MM_AUDIO := true
AUDIO_FEATURE_ENABLED_SVA_MULTI_STAGE := true
##AUDIO_FEATURE_FLAGS

ifneq ($(strip $(TARGET_USES_RRO)), true)
#Audio Specific device overlays
DEVICE_PACKAGE_OVERLAYS += $(call project-path-for,qcom-audio)/configs/common/overlay
endif

# Low latency audio buffer size in frames
PRODUCT_PROPERTY_OVERRIDES += \
    vendor.audio_hal.period_size=192

#disable tunnel encoding
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.tunnel.encode=false

#Buffer size in kbytes for compress offload playback
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.offload.buffer.size.kb=32

#Enable audio track offload by default
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.offload.track.enable=true

#enable voice path for PCM VoIP by default
PRODUCT_PROPERTY_OVERRIDES += \
vendor.voice.path.for.pcm.voip=true

#Enable multi channel aac through offload
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.offload.multiaac.enable=true

#Disable Multiple offload sesison
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.offload.multiple.enabled=false

#parser input buffer size(256kb) in byte stream mode
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.parser.ip.buffer.size=262144

#flac sw decoder 24 bit decode capability
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.flac.sw.decoder.24bit=true

#split a2dp DSP supported encoder list
PRODUCT_PROPERTY_OVERRIDES += \
persist.vendor.bt.a2dp_offload_cap=sbc-aptx-aptxtws-aptxhd-aac-ldac

#enable software decoders for ALAC and APE
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.use.sw.alac.decoder=true
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.use.sw.ape.decoder=true

#enable hw aac encoder by default
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.hw.aac.encoder=true

ifneq ($(GENERIC_ODM_IMAGE),true)
# Reduce client buffer size for fast audio output tracks
PRODUCT_PROPERTY_OVERRIDES += \
af.fast_track_multiplier=1

#Enable offload audio video playback by default
PRODUCT_PROPERTY_OVERRIDES += \
audio.offload.video=true

#Enable music through deep buffer
PRODUCT_PROPERTY_OVERRIDES += \
audio.deep_buffer.media=true

#audio becoming noisy intent broadcast delay
PRODUCT_PRODUCT_PROPERTIES += \
audio.sys.noisy.broadcast.delay=500

#audio device switch mute latency factor for draining unmuted data
PRODUCT_PRODUCT_PROPERTIES += \
audio.sys.mute.latency.factor=2

#audio device switch mute latency to absorb routing activities
PRODUCT_PRODUCT_PROPERTIES += \
audio.sys.routing.latency=0

#offload minimum duration set to 30sec
PRODUCT_PRODUCT_PROPERTIES += \
audio.offload.min.duration.secs=30

#offload pausetime out duration to 3 secs to inline with other outputs
PRODUCT_PRODUCT_PROPERTIES += \
audio.sys.offload.pstimeout.secs=3

#Set AudioFlinger client heap size
PRODUCT_PROPERTY_OVERRIDES += \
ro.af.client_heap_size_kbyte=7168

#enable deep buffer
PRODUCT_PROPERTY_OVERRIDES += \
media.stagefright.audio.deep=false

endif
#guard for non GENERIC_ODM_IMAGE

# Enable AAudio MMAP/NOIRQ data path.
# 2 is AAUDIO_POLICY_AUTO so it will try MMAP then fallback to Legacy path.
PRODUCT_PROPERTY_OVERRIDES += aaudio.mmap_policy=2
# Allow EXCLUSIVE then fall back to SHARED.
PRODUCT_PROPERTY_OVERRIDES += aaudio.mmap_exclusive_policy=2
PRODUCT_PROPERTY_OVERRIDES += aaudio.hw_burst_min_usec=2000

#enable mirror-link feature
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.enable.mirrorlink=false

# for HIDL related packages
PRODUCT_PACKAGES += \
    android.hardware.audio@4.0 \
    android.hardware.audio.common@4.0 \
    android.hardware.audio.common@4.0-util \
    android.hardware.audio.effect@4.0 \
    vendor.qti.hardware.audiohalext@1.0 \
    vendor.qti.hardware.audiohalext-utils

# enable audio hidl hal 5.0
PRODUCT_PACKAGES += \
    android.hardware.audio@5.0 \
    android.hardware.audio.common@5.0 \
    android.hardware.audio.common@5.0-util \
    android.hardware.audio.effect@5.0 \

# enable audio hidl hal 6.0
PRODUCT_PACKAGES += \
    android.hardware.audio@6.0 \
    android.hardware.audio.common@6.0 \
    android.hardware.audio.common@6.0-util \
    android.hardware.audio.effect@6.0 \

PRODUCT_PACKAGES_ENG += \
    VoicePrintTest \
    VoicePrintDemo

PRODUCT_PACKAGES_DEBUG += \
    AudioSettings

