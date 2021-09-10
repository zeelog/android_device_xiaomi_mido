#BOARD_USES_GENERIC_AUDIO := true
#
#AUDIO_FEATURE_FLAGS
ifeq ($(TARGET_USES_QMAA_OVERRIDE_AUDIO), false)
ifeq ($(TARGET_USES_QMAA),true)
AUDIO_USE_STUB_HAL := true
endif
endif

ifneq ($(AUDIO_USE_STUB_HAL), true)
BOARD_USES_ALSA_AUDIO := true

ifneq ($(TARGET_USES_AOSP_FOR_AUDIO), true)
USE_CUSTOM_AUDIO_POLICY := 1
AUDIO_FEATURE_QSSI_COMPLIANCE := true
AUDIO_FEATURE_ENABLED_COMPRESS_CAPTURE := false
AUDIO_FEATURE_ENABLED_COMPRESS_INPUT := true
AUDIO_FEATURE_ENABLED_CONCURRENT_CAPTURE := true
AUDIO_FEATURE_ENABLED_COMPRESS_VOIP := false
AUDIO_FEATURE_ENABLED_DYNAMIC_ECNS := true
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
AUDIO_FEATURE_ENABLED_SSR := true
AUDIO_FEATURE_ENABLED_DTS_EAGLE := false
BOARD_USES_SRS_TRUEMEDIA := false
DTS_CODEC_M_ := false
MM_AUDIO_ENABLED_SAFX := true
AUDIO_FEATURE_ENABLED_HW_ACCELERATED_EFFECTS := false
AUDIO_FEATURE_ENABLED_AUDIOSPHERE := true
AUDIO_FEATURE_ENABLED_USB_TUNNEL := true
AUDIO_FEATURE_ENABLED_A2DP_OFFLOAD := true
AUDIO_FEATURE_ENABLED_3D_AUDIO := false
AUDIO_FEATURE_ENABLED_AHAL_EXT := false
DOLBY_ENABLE := false
AUDIO_FEATURE_ENABLED_EXTENDED_COMPRESS_FORMAT := true
endif

USE_XML_AUDIO_POLICY_CONF := 1
BOARD_SUPPORTS_SOUND_TRIGGER := true
AUDIO_FEATURE_ENABLED_INSTANCE_ID := true
AUDIO_USE_DEEP_AS_PRIMARY_OUTPUT := false
AUDIO_FEATURE_ENABLED_VBAT_MONITOR := true
AUDIO_FEATURE_ENABLED_NT_PAUSE_TIMEOUT := true
AUDIO_FEATURE_ENABLED_ANC_HEADSET := true
AUDIO_FEATURE_ENABLED_CUSTOMSTEREO := true
AUDIO_FEATURE_ENABLED_FLUENCE := true
AUDIO_FEATURE_ENABLED_HDMI_EDID := true
AUDIO_FEATURE_ENABLED_HDMI_PASSTHROUGH := true
#AUDIO_FEATURE_ENABLED_KEEP_ALIVE := true
AUDIO_FEATURE_ENABLED_DISPLAY_PORT := true
AUDIO_FEATURE_ENABLED_DS2_DOLBY_DAP := false
AUDIO_FEATURE_ENABLED_HFP := true
AUDIO_FEATURE_ENABLED_INCALL_MUSIC := false
AUDIO_FEATURE_ENABLED_MULTI_VOICE_SESSIONS := true
AUDIO_FEATURE_ENABLED_KPI_OPTIMIZE := true
AUDIO_FEATURE_ENABLED_SPKR_PROTECTION := true
AUDIO_FEATURE_ENABLED_ACDB_LICENSE := false
AUDIO_FEATURE_ENABLED_DEV_ARBI := false
AUDIO_FEATURE_ENABLED_DYNAMIC_LOG := true
MM_AUDIO_ENABLED_FTM := true
TARGET_USES_QCOM_MM_AUDIO := true
AUDIO_FEATURE_ENABLED_SOURCE_TRACKING := true
AUDIO_FEATURE_ENABLED_GEF_SUPPORT := true
BOARD_SUPPORTS_QAHW := false
AUDIO_FEATURE_ENABLED_RAS := true
AUDIO_FEATURE_ENABLED_SND_MONITOR := false
AUDIO_FEATURE_ENABLED_DLKM := true
AUDIO_FEATURE_ENABLED_USB_BURST_MODE := false
AUDIO_FEATURE_ENABLED_SVA_MULTI_STAGE := true
AUDIO_FEATURE_ENABLED_BATTERY_LISTENER := false
##AUDIO_FEATURE_FLAGS

AUDIO_FEATURE_ENABLED_AUTO_HAL := true
AUDIO_FEATURE_ENABLED_EXT_HW_PLUGIN := true
AUDIO_FEATURE_ENABLED_AUDIO_CONTROL_HAL := true
ifneq ($(ENABLE_HYP),true)
AUDIO_FEATURE_ENABLED_AUTO_AUDIOD := true
endif
AUDIO_FEATURE_ENABLED_FM_TUNER_EXT := true
##AUTOMOTIVE_AUDIO_FEATURE_FLAGS

ifneq ($(strip $(TARGET_USES_RRO)), true)
#Audio Specific device overlays
DEVICE_PACKAGE_OVERLAYS += vendor/qcom/opensource/audio-hal/primary-hal/configs/common/overlay
endif

#Automotive audio specific device overlays
DEVICE_PACKAGE_OVERLAYS += vendor/qcom/opensource/audio-hal/primary-hal/configs/common_au/overlay

PRODUCT_COPY_FILES += \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msmnile_au/audio_io_policy.conf:$(TARGET_COPY_OUT_VENDOR)/etc/audio_io_policy.conf \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msmnile_au/audio_effects.conf:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects.conf \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msmnile_au/audio_effects.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msmnile_au/mixer_paths_adp.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths_adp.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msmnile_au/audio_tuning_mixer.txt:$(TARGET_COPY_OUT_VENDOR)/etc/audio_tuning_mixer.txt \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msmnile_au/sound_trigger_platform_info.xml:$(TARGET_COPY_OUT_VENDOR)/etc/sound_trigger_platform_info.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msmnile_au/graphite_ipc_platform_info.xml:$(TARGET_COPY_OUT_VENDOR)/etc/graphite_ipc_platform_info.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msmnile_au/audio_platform_info.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_platform_info.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msmnile_au/audio_platform_info_qrd.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_platform_info_qrd.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msmnile_au/mixer_paths_custom.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths_custom.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msmnile_au/sound_trigger_mixer_paths.xml:$(TARGET_COPY_OUT_VENDOR)/etc/sound_trigger_mixer_paths.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msmnile_au/audio_configs.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_configs.xml \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msmnile_au/audio_configs_stock.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_configs_stock.xml \
    frameworks/native/data/etc/android.hardware.audio.pro.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.audio.pro.xml \
    frameworks/native/data/etc/android.hardware.audio.low_latency.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.audio.low_latency.xml

#XML Audio configuration files
ifneq ($(TARGET_USES_AOSP_FOR_AUDIO), true)
PRODUCT_COPY_FILES += \
    $(TOPDIR)vendor/qcom/opensource/audio-hal/primary-hal/configs/msmnile_au/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio/audio_policy_configuration.xml
endif
PRODUCT_COPY_FILES += \
    $(TOPDIR)vendor/qcom/opensource/audio-hal/primary-hal/configs/common_au/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/a2dp_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/a2dp_audio_policy_configuration.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/usb_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/usb_audio_policy_configuration.xml \
    $(TOPDIR)vendor/qcom/opensource/audio-hal/primary-hal/configs/common/bluetooth_qti_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/bluetooth_qti_audio_policy_configuration.xml \
    $(TOPDIR)vendor/qcom/opensource/audio-hal/primary-hal/configs/common_au/car_audio_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/car_audio_configuration.xml

# Listen configuration file
PRODUCT_COPY_FILES += \
    vendor/qcom/opensource/audio-hal/primary-hal/configs/msmnile_au/listen_platform_info.xml:$(TARGET_COPY_OUT_VENDOR)/etc/listen_platform_info.xml

#Audio HAL version
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.hal.maj.version=3

# Reduce client buffer size for fast audio output tracks
PRODUCT_PROPERTY_OVERRIDES += \
    af.fast_track_multiplier=1

# Low latency audio buffer size in frames
PRODUCT_PROPERTY_OVERRIDES += \
    vendor.audio_hal.period_size=192

##fluencetype can be "fluence" or "fluencepro" or "none"
PRODUCT_PROPERTY_OVERRIDES += \
ro.vendor.audio.sdk.fluencetype=none\
persist.vendor.audio.fluence.voicecall=true\
persist.vendor.audio.fluence.voicerec=false\
persist.vendor.audio.fluence.speaker=true\
persist.vendor.audio.fluence.tmic.enabled=false

#
#snapdragon value add features
#
PRODUCT_PROPERTY_OVERRIDES += \
ro.qc.sdk.audio.ssr=false

##fluencetype can be "fluence" or "fluencepro" or "none"
PRODUCT_PROPERTY_OVERRIDES += \
ro.qc.sdk.audio.fluencetype=none\
persist.audio.fluence.voicecall=true\
persist.audio.fluence.voicerec=false\
persist.audio.fluence.speaker=true

#disable tunnel encoding
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.tunnel.encode=false

#Disable RAS Feature by default
PRODUCT_PROPERTY_OVERRIDES += \
persist.vendor.audio.ras.enabled=false

#Buffer size in kbytes for compress offload playback
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.offload.buffer.size.kb=32

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
vendor.audio.safx.pbe.enabled=false

#parser input buffer size(256kb) in byte stream mode
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.parser.ip.buffer.size=262144

#Enable 16 bit PCM offload by default
PRODUCT_PROPERTY_OVERRIDES += \
audio.offload.pcm.16bit.enable=true

#Enable 24 bit PCM offload by default
PRODUCT_PROPERTY_OVERRIDES += \
audio.offload.pcm.24bit.enable=true

#flac sw decoder 24 bit decode capability
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.flac.sw.decoder.24bit=true

#split a2dp DSP supported encoder list
PRODUCT_PROPERTY_OVERRIDES += \
persist.vendor.bt.a2dp_offload_cap=sbc-aptx-aptxtws-aptxhd-aac-ldac

# A2DP offload support
PRODUCT_PROPERTY_OVERRIDES += \
ro.bluetooth.a2dp_offload.supported=true

# Disable A2DP offload
PRODUCT_PROPERTY_OVERRIDES += \
persist.bluetooth.a2dp_offload.disabled=false

# A2DP offload DSP supported encoder list
PRODUCT_PROPERTY_OVERRIDES += \
persist.bluetooth.a2dp_offload.cap=sbc-aac-aptx-aptxhd-ldac

#enable software decoders for ALAC and APE
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.use.sw.alac.decoder=true
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.use.sw.ape.decoder=true

#enable hw aac encoder by default
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.hw.aac.encoder=true

#force offload using hardware decoders for FLAC, WMA & APE
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.use.hw.flac.decoder=true
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.use.hw.wma.decoder=true
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.use.hw.ape.decoder=true

#audio becoming noisy intent broadcast delay
PRODUCT_PROPERTY_OVERRIDES += \
audio.sys.noisy.broadcast.delay=600

#offload pausetime out duration to 3 secs to inline with other outputs
PRODUCT_PROPERTY_OVERRIDES += \
audio.sys.offload.pstimeout.secs=3

#Set AudioFlinger client heap size
PRODUCT_PROPERTY_OVERRIDES += \
ro.af.client_heap_size_kbyte=7168

#Set HAL buffer size to samples equal to 3 ms
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio_hal.in_period_size=144

#Set HAL buffer size to 3 ms
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio_hal.period_multiplier=3

#ADM Buffering size in ms
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.adm.buffering.ms=2

#enable keytone FR
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.hal.output.suspend.supported=false

#Enable AAudio MMAP/NOIRQ data path
#2 is AAUDIO_POLICY_AUTO so it will try MMAP then fallback to Legacy path
PRODUCT_PROPERTY_OVERRIDES += aaudio.mmap_policy=2
#Allow EXCLUSIVE then fall back to SHARED.
PRODUCT_PROPERTY_OVERRIDES += aaudio.mmap_exclusive_policy=2
PRODUCT_PROPERTY_OVERRIDES += aaudio.hw_burst_min_usec=2000

#enable mirror-link feature
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.enable.mirrorlink=false

#enable voicecall speaker stereo
PRODUCT_PROPERTY_OVERRIDES += \
persist.vendor.audio.voicecall.speaker.stereo=true

#enable AAC frame ctl for A2DP sinks
PRODUCT_PROPERTY_OVERRIDES += \
persist.vendor.bt.aac_frm_ctl.enabled=true
endif

#enable headset calibration
PRODUCT_PROPERTY_OVERRIDES += \
vendor.audio.volume.headset.gain.depcal=true

#enable dualmic fluence for voice communication
PRODUCT_PROPERTY_OVERRIDES += \
persist.vendor.audio.fluence.voicecomm=true

#add dynamic feature flags here
ifeq ($(TARGET_USES_AOSP_FOR_AUDIO),true)
# Generic ODM varient related
PRODUCT_ODM_PROPERTIES += \
vendor.audio.feature.a2dp_offload.enable=true \
vendor.audio.feature.afe_proxy.enable=false \
vendor.audio.feature.anc_headset.enable=false \
vendor.audio.feature.battery_listener.enable=false \
vendor.audio.feature.compr_cap.enable=false \
vendor.audio.feature.compress_in.enable=false \
vendor.audio.feature.compress_meta_data.enable=false \
vendor.audio.feature.compr_voip.enable=false \
vendor.audio.feature.concurrent_capture.enable=true  \
vendor.audio.feature.custom_stereo.enable=false \
vendor.audio.feature.display_port.enable=false \
vendor.audio.feature.dsm_feedback.enable=false \
vendor.audio.feature.dynamic_ecns.enable=false \
vendor.audio.feature.ext_hw_plugin.enable=true \
vendor.audio.feature.external_dsp.enable=true  \
vendor.audio.feature.external_speaker.enable=true  \
vendor.audio.feature.external_speaker_tfa.enable=false \
vendor.audio.feature.fluence.enable=false \
vendor.audio.feature.fm.enable=false \
vendor.audio.feature.hdmi_edid.enable=false \
vendor.audio.feature.hdmi_passthrough.enable=false \
vendor.audio.feature.hfp.enable=true  \
vendor.audio.feature.hifi_audio.enable=false \
vendor.audio.feature.hwdep_cal.enable=false  \
vendor.audio.feature.incall_music.enable=true  \
vendor.audio.feature.keep_alive.enable=false \
vendor.audio.feature.kpi_optimize.enable=false \
vendor.audio.feature.maxx_audio.enable=false  \
vendor.audio.feature.ras.enable=false \
vendor.audio.feature.record_play_concurency.enable=false \
vendor.audio.feature.src_trkn.enable=false \
vendor.audio.feature.spkr_prot.enable=false  \
vendor.audio.feature.ssrec.enable=false \
vendor.audio.feature.usb_offload.enable=true \
vendor.audio.feature.usb_offload_burst_mode.enable=false  \
vendor.audio.feature.usb_offload_sidetone_volume.enable=false \
vendor.audio.feature.deepbuffer_as_primary.enable=false \
vendor.audio.feature.vbat.enable=false \
vendor.audio.feature.wsa.enable=false \
vendor.audio.feature.audiozoom.enable=false \
vendor.audio.feature.snd_mon.enable=false \
vendor.audio.feature.auto_hal.enable=true
else
# Non-Generic ODM varient related
PRODUCT_ODM_PROPERTIES += \
vendor.audio.feature.a2dp_offload.enable=true \
vendor.audio.feature.afe_proxy.enable=true \
vendor.audio.feature.anc_headset.enable=true \
vendor.audio.feature.battery_listener.enable=true \
vendor.audio.feature.compr_cap.enable=false \
vendor.audio.feature.compress_in.enable=true \
vendor.audio.feature.compress_meta_data.enable=true \
vendor.audio.feature.compr_voip.enable=false \
vendor.audio.feature.concurrent_capture.enable=true \
vendor.audio.feature.custom_stereo.enable=true \
vendor.audio.feature.display_port.enable=true \
vendor.audio.feature.dsm_feedback.enable=false \
vendor.audio.feature.dynamic_ecns.enable=true \
vendor.audio.feature.ext_hw_plugin.enable=true \
vendor.audio.feature.external_dsp.enable=false \
vendor.audio.feature.external_speaker.enable=false \
vendor.audio.feature.external_speaker_tfa.enable=false \
vendor.audio.feature.fluence.enable=true \
vendor.audio.feature.fm.enable=true \
vendor.audio.feature.hdmi_edid.enable=true \
vendor.audio.feature.hdmi_passthrough.enable=true \
vendor.audio.feature.hfp.enable=true \
vendor.audio.feature.hifi_audio.enable=false \
vendor.audio.feature.hwdep_cal.enable=false \
vendor.audio.feature.incall_music.enable=true \
vendor.audio.feature.keep_alive.enable=true \
vendor.audio.feature.kpi_optimize.enable=true \
vendor.audio.feature.maxx_audio.enable=false \
vendor.audio.feature.ras.enable=true \
vendor.audio.feature.record_play_concurency.enable=false \
vendor.audio.feature.src_trkn.enable=true \
vendor.audio.feature.spkr_prot.enable=false \
vendor.audio.feature.ssrec.enable=true \
vendor.audio.feature.usb_offload.enable=true \
vendor.audio.feature.usb_offload_burst_mode.enable=true \
vendor.audio.feature.usb_offload_sidetone_volume.enable=false \
vendor.audio.feature.deepbuffer_as_primary.enable=false \
vendor.audio.feature.vbat.enable=true \
vendor.audio.feature.wsa.enable=false \
vendor.audio.feature.audiozoom.enable=false \
vendor.audio.feature.snd_mon.enable=false \
vendor.audio.feature.auto_hal.enable=true
endif

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

# enable audio hidl hal 5.0
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

PRODUCT_PACKAGES_ENG += \
    VoicePrintTest \
    VoicePrintDemo

PRODUCT_PACKAGES_DEBUG += \
    AudioSettings

# for HIDL related audiocontrol packages
PRODUCT_PACKAGES += \
    vendor.qti.hardware.automotive.audiocontrol@1.0-service \
    android.hardware.automotive.audiocontrol@1.0

ifeq ($(ENABLE_HYP),true)
PRODUCT_PROPERTY_OVERRIDES += \
persist.vendor.audio.calfile0=/vendor/etc/acdbdata/adsp_avs_config.acdb\
persist.vendor.audio.calfile1=/vendor/etc/acdbdata/ADP/Bluetooth_cal.acdb\
persist.vendor.audio.calfile2=/vendor/etc/acdbdata/ADP/Codec_cal.acdb\
persist.vendor.audio.calfile3=/vendor/etc/acdbdata/ADP/General_cal.acdb\
persist.vendor.audio.calfile4=/vendor/etc/acdbdata/ADP/Global_cal.acdb\
persist.vendor.audio.calfile5=/vendor/etc/acdbdata/ADP/Handset_cal.acdb\
persist.vendor.audio.calfile6=/vendor/etc/acdbdata/ADP/Hdmi_cal.acdb\
persist.vendor.audio.calfile7=/vendor/etc/acdbdata/ADP/Headset_cal.acdb\
persist.vendor.audio.calfile8=/vendor/etc/acdbdata/ADP/Speaker_cal.acdb
endif
