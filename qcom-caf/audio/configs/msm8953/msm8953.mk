##AUDIO_FEATURE_FLAGS
#BOARD_USES_GENERIC_AUDIO := true
BOARD_USES_ALSA_AUDIO := true

ifneq ($(TARGET_USES_AOSP_FOR_AUDIO), true)
USE_CUSTOM_AUDIO_POLICY := 1
AUDIO_FEATURE_QSSI_COMPLIANCE := true
#AUDIO_FEATURE_ENABLED_VOICE_CONCURRENCY := true
AUDIO_FEATURE_ENABLED_AAC_ADTS_OFFLOAD := true
AUDIO_FEATURE_ENABLED_COMPRESS_CAPTURE := false
AUDIO_FEATURE_ENABLED_COMPRESS_VOIP := true
AUDIO_FEATURE_ENABLED_EXTN_FORMATS := true
AUDIO_FEATURE_ENABLED_EXTN_FLAC_DECODER := true
AUDIO_FEATURE_ENABLED_EXTN_RESAMPLER := true
AUDIO_FEATURE_ENABLED_FM_POWER_OPT := true
AUDIO_FEATURE_ENABLED_PCM_OFFLOAD := true
AUDIO_FEATURE_ENABLED_PCM_OFFLOAD_24 := true
AUDIO_FEATURE_ENABLED_FLAC_OFFLOAD := true
AUDIO_FEATURE_ENABLED_VORBIS_OFFLOAD := true
AUDIO_FEATURE_ENABLED_WMA_OFFLOAD := true
AUDIO_FEATURE_ENABLED_ALAC_OFFLOAD := true
AUDIO_FEATURE_ENABLED_APE_OFFLOAD := true
AUDIO_FEATURE_ENABLED_PROXY_DEVICE := true
AUDIO_FEATURE_ENABLED_SSR := true
AUDIO_FEATURE_ENABLED_DTS_EAGLE := false
BOARD_USES_SRS_TRUEMEDIA := false
DTS_CODEC_M_ := true
#AUDIO_FEATURE_ENABLED_MULTIPLE_TUNNEL := true
MM_AUDIO_ENABLED_SAFX := true
AUDIO_FEATURE_ENABLED_HW_ACCELERATED_EFFECTS := false
AUDIO_FEATURE_ENABLED_DS2_DOLBY_DAP := false
AUDIO_FEATURE_ENABLED_AUDIOSPHERE := true
DOLBY_ENABLE := false
endif

USE_XML_AUDIO_POLICY_CONF := 1
BOARD_SUPPORTS_SOUND_TRIGGER := true
AUDIO_USE_LL_AS_PRIMARY_OUTPUT := true
AUDIO_FEATURE_ENABLED_HIFI_AUDIO := true
AUDIO_FEATURE_ENABLED_VBAT_MONITOR := true
AUDIO_FEATURE_ENABLED_NT_PAUSE_TIMEOUT := true
AUDIO_FEATURE_ENABLED_ANC_HEADSET := true
AUDIO_FEATURE_ENABLED_CUSTOMSTEREO := true
AUDIO_FEATURE_ENABLED_FLUENCE := true
AUDIO_FEATURE_ENABLED_HDMI_SPK := true
AUDIO_FEATURE_ENABLED_HDMI_EDID := true
AUDIO_FEATURE_ENABLED_EXT_HDMI := true
AUDIO_FEATURE_ENABLED_HFP := true
AUDIO_FEATURE_ENABLED_INCALL_MUSIC := true
AUDIO_FEATURE_ENABLED_MULTI_VOICE_SESSIONS := true
AUDIO_FEATURE_ENABLED_KPI_OPTIMIZE := true
AUDIO_FEATURE_ENABLED_SPKR_PROTECTION := true
AUDIO_FEATURE_ENABLED_ACDB_LICENSE := true
AUDIO_FEATURE_ENABLED_DEV_ARBI := false
MM_AUDIO_ENABLED_FTM := true
TARGET_USES_QCOM_MM_AUDIO := true
AUDIO_FEATURE_ENABLED_SOURCE_TRACKING := true
BOARD_SUPPORTS_QAHW := false
AUDIO_FEATURE_ENABLED_DYNAMIC_LOG := true
AUDIO_FEATURE_ENABLED_SND_MONITOR := true
AUDIO_FEATURE_ENABLED_SVA_MULTI_STAGE := true
ifeq ($(TARGET_KERNEL_VERSION), 3.18)
    AUDIO_FEATURE_ENABLED_DLKM := false
else
    AUDIO_FEATURE_ENABLED_DLKM := true
endif
##AUDIO_FEATURE_FLAGS

#Audio Specific device overlays
DEVICE_PACKAGE_OVERLAYS += vendor/qcom/opensource/audio-hal/primary-hal/configs/common/overlay

# Audio configuration file
ifeq ($(TARGET_USES_AOSP_FOR_AUDIO), true)
PRODUCT_COPY_FILES += \
    device/qcom/common/media/audio_policy.conf:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy.conf
else
PRODUCT_COPY_FILES += \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8953/audio_policy.conf:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy.conf
endif

PRODUCT_COPY_FILES += \
vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8953/audio_output_policy.conf:$(TARGET_COPY_OUT_VENDOR)/etc/audio_output_policy.conf \
vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8953/audio_effects.conf:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects.conf \
vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8953/audio_effects.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects.xml \
vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8953/mixer_paths_mtp.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths_mtp.xml \
vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8953/mixer_paths_tasha.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths_tasha.xml \
vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8953/mixer_paths_tashalite.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths_tashalite.xml \
vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8953/mixer_paths_sku4.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths_sku4.xml \
vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8953/mixer_paths_sku3_tasha.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths_sku3_tasha.xml \
vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8953/mixer_paths.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths.xml \
vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8953/sound_trigger_mixer_paths.xml:$(TARGET_COPY_OUT_VENDOR)/etc/sound_trigger_mixer_paths.xml \
vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8953/sound_trigger_mixer_paths_wcd9306.xml:$(TARGET_COPY_OUT_VENDOR)/etc/sound_trigger_mixer_paths_wcd9306.xml \
vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8953/sound_trigger_mixer_paths_wcd9330.xml:$(TARGET_COPY_OUT_VENDOR)/etc/sound_trigger_mixer_paths_wcd9330.xml \
vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8953/sound_trigger_mixer_paths_wcd9335.xml:$(TARGET_COPY_OUT_VENDOR)/etc/sound_trigger_mixer_paths_wcd9335.xml \
vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8953/sound_trigger_platform_info.xml:$(TARGET_COPY_OUT_VENDOR)/etc/sound_trigger_platform_info.xml \
vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8953/audio_platform_info_intcodec.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_platform_info_intcodec.xml \
vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8953/audio_platform_info_tashalite.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_platform_info_tashalite.xml \
vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8953/audio_platform_info_sku3_tasha.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_platform_info_sku3_tasha.xml \
vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8953/audio_platform_info_tasha.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_platform_info_tasha.xml \
vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8953/audio_platform_info_sku4.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_platform_info_sku4.xml \
vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8953/audio_tuning_mixer.txt:$(TARGET_COPY_OUT_VENDOR)/etc/audio_tuning_mixer.txt \
frameworks/native/data/etc/android.software.midi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.midi.xml

#XML Audio configuration files
ifeq ($(USE_XML_AUDIO_POLICY_CONF), 1)
ifneq ($(TARGET_USES_AOSP_FOR_AUDIO), true)
PRODUCT_COPY_FILES += \
    $(TOPDIR)vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8953/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio/audio_policy_configuration.xml
endif
PRODUCT_COPY_FILES += \
    $(TOPDIR)vendor/qcom/opensource/audio-hal/primary-hal/configs/msm8953/audio_policy_configuration_common.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    $(TOPDIR)vendor/qcom/opensource/audio-hal/primary-hal/configs/common/bluetooth_qti_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/a2dp_audio_policy_configuration.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/usb_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/usb_audio_policy_configuration.xml
endif

# Reduce client buffer size for fast audio output tracks
PRODUCT_PROPERTY_OVERRIDES += \
af.fast_track_multiplier=1

#Low latency audio buffer size in frames
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio_hal.period_size=192

##fluencetype can be "fluence" or "fluencepro" or "none"
PRODUCT_PROPERTY_OVERRIDES += \
ro.vendor.audio.sdk.fluencetype=none\
persist.vendor.audio.fluence.voicecall=true\
persist.vendor.audio.fluence.voicerec=false\
persist.vendor.audio.fluence.speaker=true

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

#Enable audio track offload by default
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.offload.track.enable=true

#Enable music through deep buffer
PRODUCT_PROPERTY_OVERRIDES += \
audio.deep_buffer.media=true

#enable voice path for PCM VoIP by default
PRODUCT_PROPERTY_OVERRIDES += \
vendor.voice.path.for.pcm.voip=true

#Enable multi channel aac through offload
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.offload.multiaac.enable=true

#Enable DS2, Hardbypass feature for Dolby
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.dolby.ds2.enabled=false\
vendor.audio.dolby.ds2.hardbypass=false

#Disable Multiple offload sesison
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.offload.multiple.enabled=false

#Disable Compress passthrough playback
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.offload.passthrough=false

#Disable surround sound recording
PRODUCT_PROPERTY_OVERRIDES += \
ro.vendor.audio.sdk.ssr=false

#enable dsp gapless mode by default
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.offload.gapless.enabled=true

#enable pbe effects
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.safx.pbe.enabled=true

#parser input buffer size(256kb) in byte stream mode
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.parser.ip.buffer.size=262144

#enable downsampling for multi-channel content > 48Khz
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.playback.mch.downsample=true

#enable software decoders for ALAC and APE.
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.use.sw.alac.decoder=true\
vendor.audio.use.sw.ape.decoder=true

#property for AudioSphere Post processing
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.pp.asphere.enabled=false

#Audio voice concurrency related flags
PRODUCT_PROPERTY_OVERRIDES += \
vendor.voice.playback.conc.disabled=true\
vendor.voice.record.conc.disabled=false\
vendor.voice.voip.conc.disabled=true

#Decides the audio fallback path during voice call,
#deep-buffer and fast are the two allowed fallback paths now.
PRODUCT_PROPERTY_OVERRIDES += \
vendor.voice.conc.fallbackpath=deep-buffer

#Disable speaker protection by default
PRODUCT_PROPERTY_OVERRIDES += \
persist.vendor.audio.speaker.prot.enable=false

#Enable HW AAC Encoder by default
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.hw.aac.encoder=true

#flac sw decoder 24 bit decode capability
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.flac.sw.decoder.24bit=true

#timeout crash duration set to 20sec before system is ready.
#timeout duration updates to default timeout of 5sec once the system is ready.
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.hal.boot.timeout.ms=20000

#read wsatz name from thermal zone type
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.read.wsatz.type=true

#Set AudioFlinger client heap size
PRODUCT_PROPERTY_OVERRIDES += \
ro.af.client_heap_size_kbyte=7168

PRODUCT_PROPERTY_OVERRIDES += \
persist.vendor.audio.hw.binder.size_kbyte=1024

#Set speaker protection cal tx path sampling rate to 48k
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.spkr_prot.tx.sampling_rate=48000

# add dynamic feature flags here
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.feature.snd_mon.enable=true \
vendor.audio.feature.compr_cap.enable=false \
vendor.audio.feature.hifi_audio.enable=true \
vendor.audio.feature.hdmi_edid.enable=true  \
endor.audio.feature.spkr_prot.enable=true  \
vendor.audio.feature.dsm_feedback.enable=false \
vendor.audio.feature.ssrec.enable=true  \
vendor.audio.feature.compr_voip.enable=true \
vendor.audio.feature.kpi_optimize.enable=true \
vendor.audio.feature.usb_offload.enable=false  \
vendor.audio.feature.usb_offload_burst_mode.enable=false \
vendor.audio.feature.usb_offload_sidetone_volume.enable=false \
vendor.audio.feature.src_trkn.enable=true \
vendor.audio.feature.ras.enable=false \
vendor.audio.feature.a2dp_offload.enable=false \
vendor.audio.feature.wsa.enable=true \
vendor.audio.feature.compress_meta_data.enable=true \
vendor.audio.feature.vbat.enable=true \
vendor.audio.feature.display_port.enable=false \
vendor.audio.feature.fluence.enable=true \
vendor.audio.feature.custom_stereo.enable=true \
vendor.audio.feature.anc_headset.enable=true \
vendor.audio.feature.spkr_prot.enable=false \
vendor.audio.feature.fm.enable=true \
vendor.audio.feature.external_dsp.enable=false \
vendor.audio.feature.external_speaker.enable=false \
vendor.audio.feature.external_speaker_tfa.enable=false \
vendor.audio.feature.hwdep_cal.enable=false \
vendor.audio.feature.hfp.enable=true \
vendor.audio.feature.ext_hw_plugin.enable=false \
vendor.audio.feature.record_play_concurency.enable=false \
vendor.audio.feature.hdmi_passthrough.enable=false \
vendor.audio.feature.concurrent_capture.enable=false \
vendor.audio.feature.compress_in.enable=false \
vendor.audio.feature.battery_listener.enable=false \
vendor.audio.feature.maxx_audio.enable=false \
vendor.audio.feature.audiozoom.enable=false \
vendor.audio.feature.auto_hal.enable=false \
vendor.audio.read.wsatz.type=true \
vendor.audio.feature.multi_voice_session.enable=true \
vendor.audio.feature.incall_music.enable=true

# for HIDL related packages
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

# enable audio hidl hal 5.0 for sdk rev 29 and above
ifeq ($(shell expr $(PLATFORM_SDK_VERSION) \>= 29), 1)
PRODUCT_PACKAGES += \
            android.hardware.audio@5.0 \
            android.hardware.audio.common@5.0 \
            android.hardware.audio.common@5.0-util \
            android.hardware.audio@5.0-impl \
            android.hardware.audio.effect@5.0 \
            android.hardware.audio.effect@5.0-impl

# enable audio hidl hal 6.0
PRODUCT_PACKAGES += \
            android.hardware.audio@6.0 \
            android.hardware.audio.common@6.0 \
            android.hardware.audio.common@6.0-util \
            android.hardware.audio@6.0-impl \
            android.hardware.audio.effect@6.0 \
            android.hardware.audio.effect@6.0-impl

endif
