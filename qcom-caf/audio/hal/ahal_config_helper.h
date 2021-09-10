/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef __LP64__
#define AUDIO_CONFIGSTORE_LIB_PATH "/vendor/lib64/libaudioconfigstore.so"
#else
#define AUDIO_CONFIGSTORE_LIB_PATH "/vendor/lib/libaudioconfigstore.so"
#endif

// AHalValues must be in sync with AHalValues_t in libaudioconfigstore
typedef struct {
    bool snd_monitor_enabled;
    bool compress_capture_enabled;
    bool source_track_enabled;
    bool ssrec_enabled;
    bool audiosphere_enabled;
    bool afe_proxy_enabled;
    bool use_deep_buffer_as_primary_output;
    bool hdmi_edid_enabled;
    bool keep_alive_enabled;
    bool hifi_audio_enabled;
    bool receiver_aided_stereo;
    bool kpi_optimize_enabled;
    bool display_port_enabled;
    bool fluence_enabled;
    bool custom_stereo_enabled;
    bool anc_headset_enabled;
    bool spkr_prot_enabled;
    bool fm_power_opt_enabled;
    bool ext_qdsp_enabled;
    bool ext_spkr_enabled;
    bool ext_spkr_tfa_enabled;
    bool hwdep_cal_enabled;
    bool dsm_feedback_enabled;
    bool usb_offload_enabled;
    bool usb_offload_burst_mode;
    bool usb_offload_sidetone_vol_enabled;
    bool a2dp_offload_enabled;
    bool hfp_enabled;
    bool vbat_enabled;
    bool wsa_enabled;
    bool ext_hw_plugin_enabled;
    bool record_play_concurrency;
    bool hdmi_passthrough_enabled;
    bool concurrent_capture_enabled;
    bool compress_in_enabled;
    bool battery_listener_enabled;
    bool maxx_audio_enabled;
    bool compress_metadata_needed;
    bool incall_music_enabled;
    bool compress_voip_enabled;
    bool dynamic_ecns_enabled;
    bool audio_zoom_enabled;
} AHalValues;

#ifdef __cplusplus
extern "C" {
#endif
void audio_extn_ahal_config_helper_init(bool isVendorEnhancedFwk);
void audio_extn_get_feature_values(AHalValues* *confValues);
#ifdef __cplusplus
}
#endif

