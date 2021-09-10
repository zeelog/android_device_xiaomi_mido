/* Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
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

#ifndef AUDIO_HAL_PLUGIN_H
#define AUDIO_HAL_PLUGIN_H

#if defined(__cplusplus)
extern "C" {
#endif

#include <system/audio.h>

#define AUDIO_HAL_PLUGIN_EOK (0)
#define AUDIO_HAL_PLUGIN_EFAIL (-1) /**< Undefined error */
#define AUDIO_HAL_PLUGIN_ENOMEM (-2) /**< Out of memory */
#define AUDIO_HAL_PLUGIN_EINVAL (-3) /**< Invalid argument */
#define AUDIO_HAL_PLUGIN_EBUSY (-4) /**< Plugin driver is busy */
#define AUDIO_HAL_PLUGIN_ENODEV (-5) /**< No device */
#define AUDIO_HAL_PLUGIN_EALREADY (-6) /**< Already done */

#define AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_MSG_TYPE     "ext_hw_plugin_msg_type"
#define AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_UC           "ext_hw_plugin_usecase"
#define AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_SND_DEVICE   "ext_hw_plugin_snd_device"
#define AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_DIRECTION    "ext_hw_plugin_direction"
#define AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_CMASK        "ext_hw_plugin_channel_mask"
#define AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_GAIN         "ext_hw_plugin_gain"
#define AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_MUTE_FLAG    "ext_hw_plugin_mute_flag"
#define AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_FADE         "ext_hw_plugin_fade"
#define AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_BALANCE      "ext_hw_plugin_balance"
#define AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_BMT_FTYPE    "ext_hw_plugin_bmt_filter_type"
#define AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_BMT_FLAG     "ext_hw_plugin_bmt_flag"
#define AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_BMT_VAL      "ext_hw_plugin_bmt_value"
#define AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_EQ_FLAG      "ext_hw_plugin_eq_flag"
#define AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_EQ_ID        "ext_hw_plugin_eq_id"
#define AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_EQ_NUM_BANDS "ext_hw_plugin_eq_num_bands"
#define AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_EQ_BAND_DATA "ext_hw_plugin_eq_band_data"
#define AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_TUNNEL_SIZE  "ext_hw_plugin_tunnel_size"
#define AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_TUNNEL_DATA  "ext_hw_plugin_tunnel_data"
#define AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_GETPARAM_RESULT "ext_hw_plugin_getparam_result"
#define AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_GETPARAM_DATA "ext_hw_plugin_getparam_data"
#define AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_TUNNEL_GET_SIZE "ext_hw_plugin_tunnel_get_size"
/**
 * Type of audio hal plug-in messages
 */
typedef enum
{
    AUDIO_HAL_PLUGIN_MSG_INVALID = 0,
    AUDIO_HAL_PLUGIN_MSG_CODEC_ENABLE, /**< setup codec routing path */
    AUDIO_HAL_PLUGIN_MSG_CODEC_DISABLE, /**< tear down routing path */
    AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_VOLUME, /**< set volume */
    AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_MUTE, /**< mute/unmute control */
    AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_FADE, /**< fade out control */
    AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_BALANCE, /**< left/right balance control */
    AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_BMT, /**< base/mid/treble control */
    AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_EQ, /**< EQ control */
    AUDIO_HAL_PLUGIN_MSG_CODEC_TUNNEL_CMD, /**< pass through cmds */
    AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_VOLUME, /**< get volume params */
    AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_FADE, /**< get fade params */
    AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_BALANCE, /**< get balance params */
    AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_BMT, /**< get bmt params */
    AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_EQ, /**< get EQ params */
    AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_EQ_SUBBANDS, /**< get EQ subbands params */
    AUDIO_HAL_PLUGIN_MSG_CODEC_TUNNEL_GET_CMD, /**< pass through get cmds */
    AUDIO_HAL_PLUGIN_MSG_MAX
} audio_hal_plugin_msg_type_t;

/**
 * Type of audio hal plug-in use cases
 */
typedef enum
{
    AUDIO_HAL_PLUGIN_USECASE_INVALID = -1,
    AUDIO_HAL_PLUGIN_USECASE_DEFAULT_PLAYBACK = 0,
    AUDIO_HAL_PLUGIN_USECASE_DEFAULT_CAPTURE,
    AUDIO_HAL_PLUGIN_USECASE_DRIVER_SIDE_PLAYBACK,
    AUDIO_HAL_PLUGIN_USECASE_HFP_VOICE_CALL,
    AUDIO_HAL_PLUGIN_USECASE_CS_VOICE_CALL,
    AUDIO_HAL_PLUGIN_USECASE_FM_TUNER,
    AUDIO_HAL_PLUGIN_USECASE_ICC,
    AUDIO_HAL_PLUGIN_USECASE_EC_CAPTURE,
    AUDIO_HAL_PLUGIN_USECASE_EC_REF_CAPTURE,
    AUDIO_HAL_PLUGIN_USECASE_ANC,
    AUDIO_HAL_PLUGIN_USECASE_LINE_IN_PASSTHROUGH,
    AUDIO_HAL_PLUGIN_USECASE_HDMI_IN_PASSTHROUGH,
    AUDIO_HAL_PLUGIN_USECASE_PHONE_PLAYBACK,
    AUDIO_HAL_PLUGIN_USECASE_MAX
} audio_hal_plugin_usecase_type_t;

/**
 * Type of audio hal plug-in direction used in set_param
 */
typedef enum
{
    AUDIO_HAL_PLUGIN_DIRECTION_INVALID = -1,
    AUDIO_HAL_PLUGIN_DIRECTION_PLAYBACK = 0,
    AUDIO_HAL_PLUGIN_DIRECTION_CAPTURE,
    AUDIO_HAL_PLUGIN_DIRECTION_MAX
} audio_hal_plugin_direction_type_t;

/**
 * Type of query status mask
 */
#define QUERY_VALUE_VALID            (0x0)
#define QUERY_VALUE_NOT_SUPPORTED    (0x1)
#define QUERY_VALUE_NOT_SET          (0x2)

/**
 * Type of signed 32-bit bounded value used in get_param
 */
typedef struct audio_hal_plugin_bint32
{
    uint32_t query_status_mask; /**< status of returned actual value */
    int32_t value; /**< actual value */
    int32_t min; /**< minimum for value */
    int32_t max; /**< maximum for value */
} audio_hal_plugin_bint32_t;

/**
 * Type of unsigned 32-bit bounded value used in get_param
 */
typedef struct audio_hal_plugin_buint32
{
    uint32_t query_status_mask; /**< status of returned actual value */
    uint32_t value; /**< actual value */
    uint32_t min; /**< minimum for value */
    uint32_t max; /**< maximum for value */
} audio_hal_plugin_buint32_t;

/**
 * Payload of AUDIO_HAL_PLUGIN_MSG_CODEC_ENABLE message
 */
typedef struct audio_hal_plugin_codec_enable
{
    int snd_dev;  /**< Requested endpoint device to be enabled. @enum: SND_DEVICE_XXX */
    audio_hal_plugin_usecase_type_t usecase;
            /**< Requested use case. @enum: AUDIO_HAL_PLUGIN_USECASE_XXX */
    uint32_t sample_rate;  /**< Requested sample rate for the endpoint device */
    uint32_t bit_width;  /**< Requested bit width per sample for the endpoint device */
    uint32_t num_chs;  /**< Requested number of channels for the endpoint device */
} audio_hal_plugin_codec_enable_t;

/**
 * Payload of AUDIO_HAL_PLUGIN_MSG_CODEC_DISABLE message
 */
typedef struct audio_hal_plugin_codec_disable
{
    int snd_dev; /**< Requested the endpoint device to be disabled */
    audio_hal_plugin_usecase_type_t usecase; /**< Requested use case */
} audio_hal_plugin_codec_disable_t;

/**
 * Payload of AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_VOLUME message
 */
typedef struct audio_hal_plugin_codec_set_pp_vol
{
    int snd_dev; /**< The requested endpoint device */
    audio_hal_plugin_usecase_type_t usecase; /**< Requested use case */
    audio_channel_mask_t ch_mask; /**< Requested audio channel mask */
    uint32_t gain; /**< The requested volume setting. */
} audio_hal_plugin_codec_set_pp_vol_t;

/**
 * Payload of AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_MUTE message
 */
typedef struct audio_hal_plugin_codec_set_pp_mute
{
    int snd_dev; /**< The requested endpoint device */
    audio_hal_plugin_usecase_type_t usecase; /**< Requested use case */
    audio_channel_mask_t ch_mask; /**< Requested audio channel mask */
    bool flag; /**< Enable/Disable mute flag. 1: mute, 0: unmute */
} audio_hal_plugin_codec_set_pp_mute_t;

/**
 * Payload of AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_FADE message
 */
typedef struct audio_hal_plugin_codec_set_pp_fade
{
    int snd_dev; /**< The requested endpoint device */
    audio_hal_plugin_usecase_type_t usecase; /**< Requested use case */
    int32_t fade; /**< The requested fade configuration. */
} audio_hal_plugin_codec_set_pp_fade_t;

/**
 * Payload of AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_BALANCE message
 */
typedef struct audio_hal_plugin_codec_set_pp_balance
{
    int snd_dev; /**< The requested endpoint device */
    audio_hal_plugin_usecase_type_t usecase; /**< Requested use case */
    int32_t balance; /**< The requested balance configuration. */
} audio_hal_plugin_codec_set_pp_balance_t;

/**
 * Payload of AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_BMT message
 */
typedef enum
{
    AUDIO_HAL_PLUGIN_CODEC_PP_FILTER_TYPE_INVALID = 0,
    AUDIO_HAL_PLUGIN_CODEC_PP_FILTER_TYPE_BASS,
    AUDIO_HAL_PLUGIN_CODEC_PP_FILTER_TYPE_MID,
    AUDIO_HAL_PLUGIN_CODEC_PP_FILTER_TYPE_TREBLE,
    AUDIO_HAL_PLUGIN_CODEC_PP_FILTER_TYPE_MAX
} audio_hal_plugin_codec_pp_filter_type_t;

typedef struct audio_hal_plugin_codec_set_pp_bmt
{
    int snd_dev; /**< The requested endpoint device */
    audio_hal_plugin_usecase_type_t usecase; /**< Requested use case */
    audio_hal_plugin_codec_pp_filter_type_t filter_type; /**< Requested filter type */
    bool enable_flag; /**< Enable flag. 0 - Disable, 1 - Enable */
    int32_t value; /**< Requested value to be set */
} audio_hal_plugin_codec_set_pp_bmt_t;

/**
 * Payload of AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_EQ message
 */
typedef struct audio_hal_plugin_codec_pp_eq_subband
{
    uint32_t band_idx; /**< Band index. Supported value: 0 to (num_bands - 1) */
    uint32_t center_freq; /**< Filter band center frequency in millihertz */
    int32_t band_level; /**< Filter band gain in millibels */
} audio_hal_plugin_codec_pp_eq_subband_t;

typedef struct audio_hal_plugin_codec_set_pp_eq
{
    int snd_dev; /**< The requested endpoint device */
    audio_hal_plugin_usecase_type_t usecase; /**< Requested use case */
    bool enable_flag; /**< Enable flag. 0 - Disable, 1 - Enable */
    int32_t preset_id; /**< Specify to use either pre-defined preset EQ or
                                        user-customized equalizers:
                                        -1      - custom equalizer speficied through 'bands' struct
                                        0 to N - pre-defined preset EQ index: ROCK/JAZZ/POP, etc */
    uint32_t num_bands; /**< Number of EQ subbands when a custom preset_id is selected */
    audio_hal_plugin_codec_pp_eq_subband_t *bands; /**< Equalizer sub-band struct list */
} audio_hal_plugin_codec_set_pp_eq_t;

/**
 * Payload of AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_VOLUME message
 */
typedef struct audio_hal_plugin_codec_get_pp_vol
{
    int snd_dev; /**< Requested endpoint device */
    audio_hal_plugin_usecase_type_t usecase; /**< Requested use case */
    audio_channel_mask_t ch_mask; /**< Requested audio channel mask */
    audio_hal_plugin_buint32_t ret_gain; /**< Returned volume range and value */
} audio_hal_plugin_codec_get_pp_vol_t;

/**
 * Payload of AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_FADE message
 */
typedef struct audio_hal_plugin_codec_get_pp_fade
{
    int snd_dev; /**< The requested endpoint device */
    audio_hal_plugin_usecase_type_t usecase; /**< Requested use case */
    audio_hal_plugin_bint32_t ret_fade; /**< Returned fade range and value. */
} audio_hal_plugin_codec_get_pp_fade_t;

/**
 * Payload of AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_BALANCE message
 */
typedef struct audio_hal_plugin_codec_get_pp_balance
{
    int snd_dev; /**< The requested endpoint device */
    audio_hal_plugin_usecase_type_t usecase; /**< Requested use case */
    audio_hal_plugin_bint32_t ret_balance; /**< Returned balance range and value. */
} audio_hal_plugin_codec_get_pp_balance_t;

/**
 * Payload of AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_BMT message
 */
typedef struct audio_hal_plugin_codec_get_pp_bmt
{
    int snd_dev; /**< The requested endpoint device */
    audio_hal_plugin_usecase_type_t usecase; /**< Requested use case */
    audio_hal_plugin_codec_pp_filter_type_t filter_type; /**< Requested filter type */
    audio_hal_plugin_bint32_t ret_value; /**< Returned range and value */
} audio_hal_plugin_codec_get_pp_bmt_t;

/**
 * Payload of AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_EQ message
 */
typedef struct audio_hal_plugin_codec_get_pp_eq
{
    int snd_dev; /**< The requested endpoint device */
    audio_hal_plugin_usecase_type_t usecase; /**< Requested use case */
    audio_hal_plugin_bint32_t ret_preset_id; /**< Returned preset id
                                        -1      - custom equalizer speficied through 'bands' struct
                                        0 to N - pre-defined preset EQ index: ROCK/JAZZ/POP, etc */
    uint32_t ret_num_bands; /**< Returned number of EQ subbands supported
                                          when a custom preset_id is selected */
} audio_hal_plugin_codec_get_pp_eq_t;

/**
 * Eq_subband struct used in the following payload
 */
typedef struct audio_hal_plugin_pp_eq_subband_binfo
{
    audio_hal_plugin_buint32_t ret_center_freq; /**< Returned band center frequency range
                                                                                            and value in millihertz */
    audio_hal_plugin_bint32_t ret_band_level; /**< Returned band gain range and value in millibels */
} audio_hal_plugin_pp_eq_subband_binfo_t;

/**
 * Payload of AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_EQ_SUBBANDS message
 */
typedef struct audio_hal_plugin_codec_get_pp_eq_subbands
{
    int snd_dev; /**< The requested endpoint device */
    audio_hal_plugin_usecase_type_t usecase; /**< Requested use case */
    uint32_t num_bands; /**< number of EQ subbands supported for custom eq
                                          returned from get_pp_eq query */
    audio_hal_plugin_pp_eq_subband_binfo_t *ret_bands; /**< Returned subband info list */
} audio_hal_plugin_codec_get_pp_eq_subbands_t;

/**
 * Payload of AUDIO_HAL_PLUGIN_MSG_CODEC_TUNNEL_GET_CMD message
 */
typedef struct audio_hal_plugin_codec_tunnel_get
{
    int32_t *param_data; /**< Request param data from client */
    uint32_t param_size; /**< Request 32-bit data size from client */
    uint32_t size_to_get; /**< Expected 32-bit data size to get from cleint */
    int32_t *ret_data; /**< Returned data */
    uint32_t ret_size; /**< Returned 32-bit data size */
} audio_hal_plugin_codec_tunnel_get_t;

/**
 * Initialize the audio hal plug-in module and underlying hw driver
 * One time call at audio hal boot up time
 */
int32_t audio_hal_plugin_init (void);

/**
 * De-Initialize the audio hal plug-in module and underlying hw driver
 * One time call when audio hal get unloaded from system
 */
int32_t audio_hal_plugin_deinit (void);

/**
 * Function to invoke the underlying HW driver realizing the functionality for a given use case.
 */
int32_t audio_hal_plugin_send_msg (
             audio_hal_plugin_msg_type_t msg,
             void * payload, uint32_t payload_size);

#if defined(__cplusplus)
}  /* extern "C" */
#endif

#endif
