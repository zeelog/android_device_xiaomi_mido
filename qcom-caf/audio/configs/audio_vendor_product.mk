#MM_AUDIO product packages
MM_AUDIO += audiod
MM_AUDIO += libacdbloader
MM_AUDIO += libalsautils
MM_AUDIO += libaudcal
MM_AUDIO += libaudioalsa
MM_AUDIO += libaudioparsers
MM_AUDIO += libaudioconfigstore
MM_AUDIO += libcsd-client
MM_AUDIO += lib_iec_60958_61937
MM_AUDIO += libmm-audio-resampler
MM_AUDIO += libstagefright_soft_qtiflacdec
MM_AUDIO += QCAudioManager
MM_AUDIO += liblistensoundmodel
MM_AUDIO += liblistensoundmodel2
MM_AUDIO += liblsmclient
MM_AUDIO += libcapiv2svacnn
MM_AUDIO += libcapiv2vop
MM_AUDIO += libcapiv2svarnn
MM_AUDIO += liblistensoundmodel2vendor
MM_AUDIO += libsvacnnvendor
MM_AUDIO += libsvarnncnnvendor
MM_AUDIO += libcapiv2svacnnvendor
MM_AUDIO += libcapiv2vopvendor
MM_AUDIO += libcapiv2svarnnvendor
MM_AUDIO += libpdksvavendor
MM_AUDIO += liblisten
MM_AUDIO += liblistenhardware
MM_AUDIO += STApp
MM_AUDIO += libqtigef
MM_AUDIO += libqcbassboost
MM_AUDIO += libqcvirt
MM_AUDIO += libqcreverb
MM_AUDIO += libasphere
MM_AUDIO += audio_effects.conf
MM_AUDIO += ftm_test_config
MM_AUDIO += libFlacSwDec
MM_AUDIO += libAlacSwDec
MM_AUDIO += libApeSwDec
MM_AUDIO += libMpeghSwEnc
MM_AUDIO += libdsd2pcm
MM_AUDIO += audioflacapp
MM_AUDIO += libqct_resampler
MM_AUDIO += libaudiodevarb
MM_AUDIO += audiod
MM_AUDIO += libsmwrapper
MM_AUDIO += libadpcmdec
MM_AUDIO += libmulawdec
MM_AUDIO += sound_trigger.primary.$(TARGET_BOARD_PLATFORM)
MM_AUDIO += sound_trigger_test
MM_AUDIO += libhwdaphal
MM_AUDIO += libqcomvisualizer
MM_AUDIO += libqcomvoiceprocessing
MM_AUDIO += libqcompostprocbundle
MM_AUDIO += libqvop-service
MM_AUDIO += libqvop-algo-jni.qti
MM_AUDIO += qvop-daemon
MM_AUDIO += VoicePrintSDK
MM_AUDIO += libadm
MM_AUDIO += libsurround_3mic_proc
MM_AUDIO += surround_sound_rec_AZ.cfg
MM_AUDIO += surround_sound_rec_5.1.cfg
MM_AUDIO += libdrc
MM_AUDIO += drc_cfg_AZ.txt
MM_AUDIO += drc_cfg_5.1.txt
MM_AUDIO += libgcs-osal
MM_AUDIO += libgcs-calwrapper
MM_AUDIO += libgcs-ipc
MM_AUDIO += libgcs
MM_AUDIO += noisesample.bin
MM_AUDIO += antispoofing.bin
MM_AUDIO += libshoebox
MM_AUDIO += libdolby_ms12_wrapper
MM_AUDIO += silence.ac3
MM_AUDIO += libaudio_ip_handler
MM_AUDIO += libsndmonitor
MM_AUDIO += libcomprcapture
MM_AUDIO += libssrec
MM_AUDIO += libhdmiedid
MM_AUDIO += libspkrprot
MM_AUDIO += libcirrusspkrprot
MM_AUDIO += liba2dpoffload
MM_AUDIO += libexthwplugin
MM_AUDIO += libhfp
MM_AUDIO += libhdmipassthru
MM_AUDIO += libbatterylistener
MM_AUDIO += libhwdepcal
MM_AUDIO += libmediaplayerservice
MM_AUDIO += libaudiohal_deathhandler
MM_AUDIO += libstagefright_httplive
MM_AUDIO += libautohal
MM_AUDIO += MTP_Bluetooth_cal.acdb
MM_AUDIO += MTP_Codec_cal.acdb
MM_AUDIO += MTP_General_cal.acdb
MM_AUDIO += MTP_Global_cal.acdb
MM_AUDIO += MTP_Handset_cal.acdb
MM_AUDIO += MTP_Hdmi_cal.acdb
MM_AUDIO += MTP_Headset_cal.acdb
MM_AUDIO += MTP_Speaker_cal.acdb
MM_AUDIO += MTP_workspaceFile.qwsp
MM_AUDIO += QRD_Bluetooth_cal.acdb
MM_AUDIO += QRD_Codec_cal.acdb
MM_AUDIO += QRD_General_cal.acdb
MM_AUDIO += QRD_Global_cal.acdb
MM_AUDIO += QRD_Handset_cal.acdb
MM_AUDIO += QRD_Hdmi_cal.acdb
MM_AUDIO += QRD_Headset_cal.acdb
MM_AUDIO += QRD_Speaker_cal.acdb
MM_AUDIO += QRD_workspaceFile.qwsp
ifeq ($(TARGET_BOARD_AUTO),true)
MM_AUDIO += adsp_avs_config.acdb
MM_AUDIO += Bluetooth_cal.acdb
MM_AUDIO += Codec_cal.acdb
MM_AUDIO += General_cal.acdb
MM_AUDIO += Global_cal.acdb
MM_AUDIO += Handset_cal.acdb
MM_AUDIO += Hdmi_cal.acdb
MM_AUDIO += Headset_cal.acdb
MM_AUDIO += Speaker_cal.acdb

MM_AUDIO += libaudiohalplugin
MM_AUDIO += libcdcdriver
MM_AUDIO += libvad
MM_AUDIO += capi_v2_bmt
MM_AUDIO += capi_v2_fnb
MM_AUDIO += capi_v2_loud
MM_AUDIO += capi_v2_peq
MM_AUDIO += capi_v2_sumx
MM_AUDIO += capi_v2_synth
MM_AUDIO += capi_v2_avc
MM_AUDIO += capi_v2_asrc
MM_AUDIO += icc_module.so.1
MM_AUDIO += sec_module.so.1
MM_AUDIO += audio-nxp-auto
MM_AUDIO += libaudio-nxp-auto
MM_AUDIO += mercuryflasher
MM_AUDIO += liba2bplugin-master
MM_AUDIO += liba2bplugin-slave
MM_AUDIO += liba2bstack
MM_AUDIO += liba2bstack-pal
MM_AUDIO += liba2bstack-protobuf
MM_AUDIO += a2b-app
MM_AUDIO += liba2bdriver
MM_AUDIO += libacdbloaderclient
MM_AUDIO += acdb_loader_service
MM_AUDIO += libaudiohalpluginclient
MM_AUDIO += audio_hal_plugin_service
MM_AUDIO += audio_chime
MM_AUDIO += libqtiautobundle
MM_AUDIO += autoeffects
MM_AUDIO += autoeffects.xml
MM_AUDIO += audcalparam_commands.cfg
MM_AUDIO += libsynth

MM_AUDIO += android.hardware.automotive.audiocontrol-service.example
MM_AUDIO += libaudiopowerpolicy
endif

ifeq ($(ENABLE_HYP), true)
MM_AUDIO += amfsservice
endif

#MM_AUDIO_DBG
MM_AUDIO_DBG += libstagefright_soft_ddpdec
MM_AUDIO_DBG += libsurround_proc
MM_AUDIO_DBG += surround_sound_headers
MM_AUDIO_DBG += filter1i.pcm
MM_AUDIO_DBG += filter1r.pcm
MM_AUDIO_DBG += filter2i.pcm
MM_AUDIO_DBG += filter2r.pcm
MM_AUDIO_DBG += filter3i.pcm
MM_AUDIO_DBG += filter3r.pcm
MM_AUDIO_DBG += filter4i.pcm
MM_AUDIO_DBG += filter4r.pcm
MM_AUDIO_DBG += mm-audio-ftm
MM_AUDIO_DBG += mm-audio-alsa-test
MM_AUDIO_DBG += avs_test_ker.ko
MM_AUDIO_DBG += libsrsprocessing_libs
MM_AUDIO_DBG += libsrsprocessing
MM_AUDIO_DBG += libacdbrtac
MM_AUDIO_DBG += libadiertac

PRODUCT_PACKAGES += $(MM_AUDIO)

PRODUCT_PACKAGES_DEBUG += $(MM_AUDIO_DBG)

#-------
# audio specific
# ------
TARGET_USES_AOSP := true
TARGET_USES_AOSP_FOR_AUDIO := true

# sdm845 specific rules
ifeq ($(TARGET_BOARD_PLATFORM),sdm845)
TARGET_USES_AOSP := false
TARGET_USES_AOSP_FOR_AUDIO := false
endif

# Audio configuration file
-include $(TOPDIR)vendor/qcom/opensource/audio-hal/primary-hal/configs/$(TARGET_BOARD_PLATFORM)$(TARGET_BOARD_SUFFIX)/$(TARGET_BOARD_PLATFORM)$(TARGET_BOARD_SUFFIX).mk

