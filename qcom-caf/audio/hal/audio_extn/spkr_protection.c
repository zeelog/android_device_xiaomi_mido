/*
 * Copyright (c) 2013 - 2020, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "audio_hw_spkr_prot"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <errno.h>
#include <math.h>
#include <log/log.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>
#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include <sys/stat.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <math.h>
#include <unistd.h>
#include <cutils/properties.h>
#include "audio_extn.h"
#include <linux/msm_audio_calibration.h>
#include <linux/msm_audio.h>

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_SPKR_PROT
#include <log_utils.h>
#endif

#ifdef SPKR_PROT_ENABLED

/*Range of spkr temparatures -30C to 80C*/
#define MIN_SPKR_TEMP_Q6 (-30 * (1 << 6))
#define MAX_SPKR_TEMP_Q6 (80 * (1 << 6))
#define VI_FEED_CHANNEL "VI_FEED_TX Channels"
#define SPKR_LEFT_WSA_TEMP "SpkrLeft WSA Temp"
#define SPKR_RIGHT_WSA_TEMP "SpkrRight WSA Temp"
#define WSA8815_SPK1_NAME "wsatz.13"
#define WSA8815_SPK2_NAME "wsatz.14"
#define WCD_LEFT_BOOST_MAX_STATE "SPKR Left Boost Max State"
#define WCD_RIGHT_BOOST_MAX_STATE "SPKR Right Boost Max State"
#define WSA_LEFT_BOOST_LEVEL "SpkrLeft Boost Level"
#define WSA_RIGHT_BOOST_LEVEL "SpkrRight Boost Level"
/* Min and max resistance value in lookup table. */
#define MIN_RESISTANCE_LOOKUP (3.2)
#define MAX_RESISTANCE_LOOKUP (8)
#define SPKR_PROT_LOOKUP_TABLE_ROWS (49)
/* default limiter threshold is 0dB(0x7FFFFFF in natural value) */
#define DEFAULT_LIMITER_TH (0x07FFFFFF)
#define AFE_API_VERSION_SUPPORT_SPV3 (0x2)
/* Made equivalent to AFE API version that supports SPV4. */
#define AFE_API_VERSION_SUPPORT_SPV4 (0x9)
enum wcd_boost_max_state {
    BOOST_NO_MAX_STATE,
    BOOST_MAX_STATE_1,
    BOOST_MAX_STATE_2,
};

enum sp_version {
    SP_V2 = 0x1,
    SP_V3 = AFE_API_VERSION_SUPPORT_SPV3,
    SP_V4 = AFE_API_VERSION_SUPPORT_SPV4,
};
/*Set safe temp value to 40C*/
#define SAFE_SPKR_TEMP 40
#define SAFE_SPKR_TEMP_Q6 (SAFE_SPKR_TEMP * (1 << 6))

/*Bongo Spkr temp range*/
#define TZ_TEMP_MIN_THRESHOLD    (5)
#define TZ_TEMP_MAX_THRESHOLD    (45)

/*Range of resistance values 2ohms to 40 ohms*/
#define MIN_RESISTANCE_SPKR_Q24 (2 * (1 << 24))
#define MAX_RESISTANCE_SPKR_Q24 (40 * (1 << 24))

/*Path where the calibration file will be stored*/
#ifdef LINUX_ENABLED
#define CALIB_FILE "/data/audio/audio.cal"
#else
#define CALIB_FILE "/data/vendor/audio/audio.cal"
#endif

/*Time between retries for calibartion or intial wait time
  after boot up*/
#define WAIT_TIME_SPKR_CALIB (60 * 1000 * 1000)

#define MIN_SPKR_IDLE_SEC (60 * 30)
#define WAKEUP_MIN_IDLE_CHECK 30

/*Once calibration is started sleep for 3 sec to allow
  the calibration to kick off*/
#define SLEEP_AFTER_CALIB_START (3000)

/*If calibration is in progress wait for 200 msec before querying
  for status again*/
#define WAIT_FOR_GET_CALIB_STATUS (200)
#define GET_SPKR_PROT_CAL_TIMEOUT_MSEC (5000)

/*Speaker states*/
#define SPKR_NOT_CALIBRATED -1
#define SPKR_CALIBRATED 1

/*Speaker processing state*/
#define SPKR_PROCESSING_IN_PROGRESS 1
#define SPKR_PROCESSING_IN_IDLE 0

/* In wsa analog mode vi feedback DAI supports at max 2 channels*/
#define WSA_ANALOG_MODE_CHANNELS 2

/* v-validation parameters */
#define SPKR_V_VALI_TEMP_MASK 0xFFFE
#define SPKR_V_VALI_DEFAULT_WAIT_TIME 500
#define SPKR_V_VALI_DEFAULT_VALI_TIME 2000
#define SPKR_V_VALI_SUCCESS 1

#define MAX_PATH             (256)
#define MAX_STR_SIZE         (1024)
#define THERMAL_SYSFS "/sys/devices/virtual/thermal"
#define TZ_TYPE "/sys/devices/virtual/thermal/thermal_zone%d/type"
#define TZ_WSA "/sys/devices/virtual/thermal/thermal_zone%d/temp"

#define AUDIO_PARAMETER_KEY_SPKR_TZ_1     "spkr_1_tz_name"
#define AUDIO_PARAMETER_KEY_SPKR_TZ_2     "spkr_2_tz_name"

#define AUDIO_PARAMETER_KEY_FBSP_TRIGGER_SPKR_CAL   "trigger_spkr_cal"
#define AUDIO_PARAMETER_KEY_FBSP_APPLY_SPKR_CAL   "apply_spkr_cal"
#define AUDIO_PARAMETER_KEY_FBSP_GET_SPKR_CAL       "get_spkr_cal"
#define AUDIO_PARAMETER_KEY_FBSP_CFG_WAIT_TIME      "fbsp_cfg_wait_time"
#define AUDIO_PARAMETER_KEY_FBSP_CFG_FTM_TIME       "fbsp_cfg_ftm_time"
#define AUDIO_PARAMETER_KEY_FBSP_GET_FTM_PARAM      "get_ftm_param"
#define AUDIO_PARAMETER_KEY_FBSP_TRIGGER_V_VALI     "trigger_v_vali"
#define AUDIO_PARAMETER_KEY_FBSP_V_VALI_WAIT_TIME   "fbsp_v_vali_wait_time"
#define AUDIO_PARAMETER_KEY_FBSP_V_VALI_VALI_TIME   "fbsp_v_vali_vali_time"

// - external function dependency -
static fp_read_line_from_file_t fp_read_line_from_file;
static fp_get_usecase_from_list_t fp_get_usecase_from_list;
static fp_enable_disable_snd_device_t fp_disable_snd_device;
static fp_enable_disable_snd_device_t  fp_enable_snd_device;
static fp_enable_disable_audio_route_t fp_disable_audio_route;
static fp_enable_disable_audio_route_t fp_enable_audio_route;
static fp_platform_set_snd_device_backend_t fp_platform_set_snd_device_backend;
static fp_platform_get_snd_device_name_extn_t fp_platform_get_snd_device_name_extn;
static fp_platform_get_default_app_type_v2_t fp_platform_get_default_app_type_v2;
static fp_platform_send_audio_calibration_t fp_platform_send_audio_calibration;
static fp_platform_get_pcm_device_id_t fp_platform_get_pcm_device_id;
static fp_platform_get_snd_device_name_t fp_platform_get_snd_device_name;
static fp_platform_spkr_prot_is_wsa_analog_mode_t fp_platform_spkr_prot_is_wsa_analog_mode;
static fp_platform_get_snd_device_t fp_platform_get_vi_feedback_snd_device;
static fp_platform_get_snd_device_t fp_platform_get_spkr_prot_snd_device;
static fp_platform_check_and_set_codec_backend_cfg_t fp_platform_check_and_set_codec_backend_cfg;
static fp_audio_extn_is_vbat_enabled_t fp_audio_extn_is_vbat_enabled;

static int get_spkr_prot_v_vali_param(int cal_fd, int *status, int *vrms);
/*Modes of Speaker Protection*/
enum speaker_protection_mode {
    SPKR_PROTECTION_DISABLED = -1,
    SPKR_PROTECTION_MODE_PROCESSING = 0,
    SPKR_PROTECTION_MODE_CALIBRATE = 1,
};

struct spkr_prot_r0t0 {
    int r0[SP_V2_NUM_MAX_SPKRS];
    int t0[SP_V2_NUM_MAX_SPKRS];
};

struct speaker_prot_session {
    int spkr_prot_mode;
    int spkr_processing_state;
    int thermal_client_handle;
    pthread_mutex_t mutex_spkr_prot;
    pthread_t spkr_calibration_thread;
    pthread_t spkr_v_vali_thread;
    pthread_mutex_t spkr_prot_thermalsync_mutex;
    pthread_cond_t spkr_prot_thermalsync;
    int cancel_spkr_calib;
    pthread_cond_t spkr_calib_cancel;
    pthread_mutex_t spkr_calib_cancelack_mutex;
    pthread_cond_t spkr_calibcancel_ack;
    pthread_t speaker_prot_threadid;
    pthread_t v_vali_threadid;
    void *thermal_handle;
    void *adev_handle;
    int spkr_prot_t0;
    struct pcm *pcm_rx;
    struct pcm *pcm_tx;
    int (*client_register_callback)
    (char *client_name, int (*callback)(int), void *data);
    void (*thermal_client_unregister_callback)(int handle);
    int (*thermal_client_request)(char *client_name, int req_data);
    bool spkr_prot_enable;
    bool spkr_in_use;
    struct timespec spkr_last_time_used;
    struct spkr_prot_r0t0 sp_r0t0_cal;
    bool wsa_found;
    bool is_wsa_temp_mixer_ctl;
    bool is_spkr1_avail;
    bool is_spkr2_avail;
    int spkr_1_tzn;
    int spkr_2_tzn;
    bool trigger_cal;
    bool trigger_v_vali;
    bool apply_cal;
    pthread_mutex_t cal_wait_cond_mutex;
    pthread_cond_t cal_wait_condition;
    bool spkr_cal_dynamic;
    volatile bool thread_exit;
    unsigned int sp_version;
    int limiter_th[SP_V2_NUM_MAX_SPKRS];
    int v_vali_wait_time;
    int v_vali_vali_time;
    bool cal_thrd_created;
    bool v_vali_thrd_created;
};

static struct pcm_config pcm_config_skr_prot = {
    .channels = 4,
    .rate = 48000,
    .period_size = 256,
    .period_count = 4,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

struct spkr_tz_names {
    char *spkr_1_name;
    char *spkr_2_name;
};

struct spkr_prot_boost {
    /* bit7-4: first stage; bit 3-0: second stage */
    int boost_value;
    int max_state;
};

#define SPKR_PROT_BOOST_VALUE_STATE(value, state) \
{    .boost_value = (value), .max_state = (state) }

static struct spkr_prot_boost spkr_prot_boost_lookup_table[SPKR_PROT_LOOKUP_TABLE_ROWS] = {
    SPKR_PROT_BOOST_VALUE_STATE(0xc7, BOOST_MAX_STATE_1),
    SPKR_PROT_BOOST_VALUE_STATE(0xd7, BOOST_MAX_STATE_1),
    SPKR_PROT_BOOST_VALUE_STATE(0xd7, BOOST_MAX_STATE_1),
    SPKR_PROT_BOOST_VALUE_STATE(0xe7, BOOST_MAX_STATE_1),
    SPKR_PROT_BOOST_VALUE_STATE(0xe7, BOOST_MAX_STATE_1),
    SPKR_PROT_BOOST_VALUE_STATE(0xf7, BOOST_MAX_STATE_1),
    SPKR_PROT_BOOST_VALUE_STATE(0x70, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x70, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x71, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x71, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x72, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x72, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x73, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x73, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x74, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x75, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x75, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x76, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x76, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x77, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x77, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x78, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x78, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x79, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x79, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7a, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7a, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7a, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7b, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7b, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7c, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7c, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7d, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7d, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7e, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7e, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7f, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7f, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7f, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7f, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7f, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7f, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7f, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7f, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7f, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7f, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7f, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7f, BOOST_MAX_STATE_2),
    SPKR_PROT_BOOST_VALUE_STATE(0x7f, BOOST_MAX_STATE_2),
};

/* 3.2 ohm in q24 format: (3.2 * (1 << 24)) */
#define MIN_LOOKUP_RESISTANCE_SPKR_Q24    (53687091)
/* 8 ohm in q24 format: (8 * (1 << 24)) */
#define MAX_LOOKUP_RESISTANCE_SPKR_Q24    (134217728)
/* 0.1 ohm in q24 format: (0.1 * (1 <<24)) */
#define LOOKUP_RESISTANCE_GAP_SPKR_Q24    (1677722)

/* 3.2ohm : 0.1ohm : 8ohm lookup table */
static int spv3_limiter_th_q27_table[SPKR_PROT_LOOKUP_TABLE_ROWS] = {
    85469248, 86758070, 88066327, 89394311, 90637910, 91898809,
    93070036, 94364769, 95567425, 96674043, 97906130, 99039829,
    100186656, 101346763, 102402340, 103588104, 104667026, 105757185,
    106858699, 107847451, 108970736, 109979029, 110996653, 112023692,
    113060235, 113975074, 115029672, 115960448, 117033416, 117980405,
    118935056, 119897432, 120867596, 121705410, 122690202, 123682964,
    124540293, 125403565, 126418282, 127294571, 128176935, 129065415,
    129960054, 130860894, 131616362, 132528683, 133447328, 134217728,
    134217728
};
static struct speaker_prot_session handle;
static int vi_feed_no_channels;
static struct spkr_tz_names tz_names;

int get_spkr_prot_snd_device(snd_device_t snd_device) {
    return snd_device;
}

/*===========================================================================
FUNCTION get_tzn

Utility function to match a sensor name with thermal zone id.

ARGUMENTS
    sensor_name - name of sensor to match

RETURN VALUE
    Thermal zone id on success,
    -1 on failure.
===========================================================================*/
int get_tzn(const char *sensor_name)
{
    DIR *tdir = NULL;
    struct dirent *tdirent = NULL;
    int found = -1;
    int tzn = 0;
    char name[MAX_PATH] = {0};
    char cwd[MAX_PATH] = {0};

    if (!sensor_name || (strlen(sensor_name) == 0))
        return found;

    if (!getcwd(cwd, sizeof(cwd)))
        return found;

    chdir(THERMAL_SYSFS); /* Change dir to read the entries. Doesnt work
                             otherwise */
    tdir = opendir(THERMAL_SYSFS);
    if (!tdir) {
        ALOGE("Unable to open %s\n", THERMAL_SYSFS);
        return found;
    }

    while ((tdirent = readdir(tdir))) {
        char buf[50];
        struct dirent *tzdirent;
        DIR *tzdir = NULL;

        tzdir = opendir(tdirent->d_name);
        if (!tzdir)
            continue;
        while ((tzdirent = readdir(tzdir))) {
            if (strcmp(tzdirent->d_name, "type"))
                continue;
            snprintf(name, MAX_PATH, TZ_TYPE, tzn);
            ALOGV("Opening %s\n", name);
            fp_read_line_from_file(name, buf, sizeof(buf));
            if (strlen(buf) > 0)
                buf[strlen(buf) - 1] = '\0';
            if (!strcmp(buf, sensor_name)) {
                ALOGD(" spkr tz name found, %s\n", name);
                found = 1;
                break;
            }
            tzn++;
        }
        closedir(tzdir);
        if (found == 1)
            break;
    }
    closedir(tdir);
    chdir(cwd); /* Restore current working dir */
    if (found == 1) {
        found = tzn;
        ALOGE("Sensor %s found at tz: %d\n", sensor_name, tzn);
    }
    return found;
}

static void spkr_prot_set_spkrstatus(bool enable)
{
    if (enable)
       handle.spkr_in_use = true;
    else {
       handle.spkr_in_use = false;
       clock_gettime(CLOCK_BOOTTIME, &handle.spkr_last_time_used);
   }
}

void spkr_prot_calib_cancel(void *adev)
{
    pthread_t threadid;
    struct audio_usecase *uc_info;
    threadid = pthread_self();
    ALOGV("%s: Entry", __func__);
    if (pthread_equal(handle.speaker_prot_threadid, threadid) || !adev ||
        pthread_equal(handle.v_vali_threadid, threadid)) {
        ALOGE("%s: Invalid params", __func__);
        return;
    }
    uc_info = fp_get_usecase_from_list(adev, USECASE_AUDIO_SPKR_CALIB_RX);
    if (uc_info) {
            pthread_mutex_lock(&handle.mutex_spkr_prot);
            pthread_mutex_lock(&handle.spkr_calib_cancelack_mutex);
            handle.cancel_spkr_calib = 1;
            pthread_cond_signal(&handle.spkr_calib_cancel);
            pthread_mutex_unlock(&handle.mutex_spkr_prot);
            pthread_cond_wait(&handle.spkr_calibcancel_ack,
            &handle.spkr_calib_cancelack_mutex);
            pthread_mutex_unlock(&handle.spkr_calib_cancelack_mutex);
    }
    ALOGV("%s: Exit", __func__);
}

static bool is_speaker_in_use(unsigned long *sec)
{
    struct timespec temp;
    if (!sec) {
        ALOGE("%s: Invalid params", __func__);
        return true;
    }
    if (handle.spkr_in_use) {
        *sec = 0;
        return true;
    } else {
        clock_gettime(CLOCK_BOOTTIME, &temp);
        *sec = temp.tv_sec - handle.spkr_last_time_used.tv_sec;
        return false;
    }
}


static int get_spkr_prot_cal(int cal_fd,
                struct audio_cal_info_msm_spk_prot_status *status)
{
    int ret = 0;
    struct audio_cal_fb_spk_prot_status    cal_data;

    if (cal_fd < 0) {
        ALOGE("%s: Error: cal_fd = %d", __func__, cal_fd);
        ret = -EINVAL;
        goto done;
    }

    if (status == NULL) {
        ALOGE("%s: Error: status NULL", __func__);
        ret = -EINVAL;
        goto done;
    }

    cal_data.hdr.data_size = sizeof(cal_data);
    cal_data.hdr.version = VERSION_0_0;
    cal_data.hdr.cal_type = AFE_FB_SPKR_PROT_CAL_TYPE;
    cal_data.hdr.cal_type_size = sizeof(cal_data.cal_type);
    cal_data.cal_type.cal_hdr.version = VERSION_0_0;
    cal_data.cal_type.cal_hdr.buffer_number = 0;
    cal_data.cal_type.cal_data.mem_handle = -1;

    if (ioctl(cal_fd, AUDIO_GET_CALIBRATION, &cal_data)) {
        ALOGE("%s: Error: AUDIO_GET_CALIBRATION failed!",
            __func__);
        ret = -ENODEV;
        goto done;
    }

    status->r0[SP_V2_SPKR_1] = cal_data.cal_type.cal_info.r0[SP_V2_SPKR_1];
    status->r0[SP_V2_SPKR_2] = cal_data.cal_type.cal_info.r0[SP_V2_SPKR_2];
    status->status = cal_data.cal_type.cal_info.status;
done:
    return ret;
}

static int set_spkr_prot_cal(int cal_fd,
                struct audio_cal_info_spk_prot_cfg *protCfg)
{
    int ret = 0;
    struct audio_cal_fb_spk_prot_cfg    cal_data;
    char value[PROPERTY_VALUE_MAX];
    static int cal_done = 0;

    if (cal_fd < 0) {
        ALOGE("%s: Error: cal_fd = %d", __func__, cal_fd);
        ret = -EINVAL;
        goto done;
    }

    if (protCfg == NULL) {
        ALOGE("%s: Error: status NULL", __func__);
        ret = -EINVAL;
        goto done;
    }

    memset(&cal_data, 0, sizeof(cal_data));
    cal_data.hdr.data_size = sizeof(cal_data);
    cal_data.hdr.version = VERSION_0_0;
    cal_data.hdr.cal_type = AFE_FB_SPKR_PROT_CAL_TYPE;
    cal_data.hdr.cal_type_size = sizeof(cal_data.cal_type);
    cal_data.cal_type.cal_hdr.version = VERSION_0_0;
    cal_data.cal_type.cal_hdr.buffer_number = 0;
    cal_data.cal_type.cal_info.r0[SP_V2_SPKR_1] = protCfg->r0[SP_V2_SPKR_1];
    cal_data.cal_type.cal_info.r0[SP_V2_SPKR_2] = protCfg->r0[SP_V2_SPKR_2];
    cal_data.cal_type.cal_info.t0[SP_V2_SPKR_1] = protCfg->t0[SP_V2_SPKR_1];
    cal_data.cal_type.cal_info.t0[SP_V2_SPKR_2] = protCfg->t0[SP_V2_SPKR_2];
    cal_data.cal_type.cal_info.mode = protCfg->mode;
#ifdef MSM_SPKR_PROT_SPV3
    cal_data.cal_type.cal_info.sp_version = protCfg->sp_version;
    cal_data.cal_type.cal_info.limiter_th[SP_V2_SPKR_1] = protCfg->limiter_th[SP_V2_SPKR_1];
    cal_data.cal_type.cal_info.limiter_th[SP_V2_SPKR_2] = protCfg->limiter_th[SP_V2_SPKR_2];
#endif
    property_get("persist.vendor.audio.spkr.cal.duration", value, "0");
    if (atoi(value) > 0) {
        ALOGD("%s: quick calibration enabled", __func__);
        cal_data.cal_type.cal_info.quick_calib_flag = 1;
    } else {
        property_get("persist.spkr.cal.duration", value, "0");
        if (atoi(value) > 0) {
            ALOGD("%s: quick calibration enabled", __func__);
            cal_data.cal_type.cal_info.quick_calib_flag = 1;
        } else {
            ALOGD("%s: quick calibration disabled", __func__);
            cal_data.cal_type.cal_info.quick_calib_flag = 0;
        }
    }

    cal_data.cal_type.cal_data.mem_handle = -1;

    if (ioctl(cal_fd, AUDIO_SET_CALIBRATION, &cal_data)) {
        ALOGE("%s: Error: AUDIO_SET_CALIBRATION failed!",
            __func__);
        ret = -ENODEV;
        goto done;
    }
    if (protCfg->mode == MSM_SPKR_PROT_CALIBRATED  && !cal_done) {
        handle.sp_r0t0_cal.r0[SP_V2_SPKR_1] = protCfg->r0[SP_V2_SPKR_1];
        handle.sp_r0t0_cal.r0[SP_V2_SPKR_2] = protCfg->r0[SP_V2_SPKR_2];
        handle.sp_r0t0_cal.t0[SP_V2_SPKR_1] = protCfg->t0[SP_V2_SPKR_1];
        handle.sp_r0t0_cal.t0[SP_V2_SPKR_2] = protCfg->t0[SP_V2_SPKR_2];
        cal_done = 1;
    }
done:
    return ret;
}

enum {
    WSA_SPKR_LEFT = 0,
    WSA_SPKR_RIGHT,
};

static int spkr_get_temp(struct audio_device *adev, int spkr_pos, int *temp)
{
    struct mixer_ctl *ctl;
    const char *mixer_ctl_name;

    ALOGV("%s: entry", __func__);
    if (spkr_pos == WSA_SPKR_LEFT)
        mixer_ctl_name = SPKR_LEFT_WSA_TEMP;
    else
        mixer_ctl_name = SPKR_RIGHT_WSA_TEMP;

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        goto error;
    }
    if (temp) {
        *temp = mixer_ctl_get_value(ctl, 0);
    }
    return 0;

error:
     return -EINVAL;
}

static int vi_feed_get_channels(struct audio_device *adev)
{
    struct mixer_ctl *ctl;
    const char *mixer_ctl_name = VI_FEED_CHANNEL;
    int value;

    ALOGV("%s: entry", __func__);
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        goto error;
    }
    value = mixer_ctl_get_value(ctl, 0);
    if (value < 0)
        goto error;
    else
        return value+1;
error:
     return -EINVAL;
}

void destroy_thread_params()
{
    pthread_mutex_destroy(&handle.mutex_spkr_prot);
    pthread_mutex_destroy(&handle.spkr_calib_cancelack_mutex);
    pthread_mutex_destroy(&handle.cal_wait_cond_mutex);
    pthread_cond_destroy(&handle.cal_wait_condition);
    pthread_cond_destroy(&handle.spkr_calib_cancel);
    pthread_cond_destroy(&handle.spkr_calibcancel_ack);
    if(!handle.wsa_found) {
        pthread_mutex_destroy(&handle.spkr_prot_thermalsync_mutex);
        pthread_cond_destroy(&handle.spkr_prot_thermalsync);
    }
}

static void check_wsa(struct audio_device *adev,
                unsigned int num_of_spkrs, bool *wsa_is_8815)
{
    unsigned int i = 0;
    if (!handle.wsa_found ||
        fp_platform_spkr_prot_is_wsa_analog_mode(adev)){
        for (i = 0; i < num_of_spkrs; i++)
            wsa_is_8815[i] = false;

        return;
    }

    if (handle.spkr_1_tzn >= 0 &&
        !strncmp(WSA8815_SPK1_NAME, tz_names.spkr_1_name, sizeof(WSA8815_SPK1_NAME)))
        wsa_is_8815[SP_V2_SPKR_1] = true;

    if (handle.spkr_2_tzn >= 0 &&
        !strncmp(WSA8815_SPK2_NAME, tz_names.spkr_2_name, sizeof(WSA8815_SPK2_NAME)))
        wsa_is_8815[SP_V2_SPKR_2] = true;
}

int set_wcd_boost_max_state(struct audio_device *adev,
                int boost_max_state, int wsa_num)
{
    struct mixer_ctl *ctl = NULL;
    const char *mixer_ctl_name[] = {
        WCD_LEFT_BOOST_MAX_STATE,
        WCD_RIGHT_BOOST_MAX_STATE
    };
    int status = 0;

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name[wsa_num]);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
                __func__, mixer_ctl_name[wsa_num]);
        return -EINVAL;
    }

    status = mixer_ctl_set_value(ctl, 0, boost_max_state);
    if (status < 0) {
        ALOGE("%s: failed to set WCD boost state.\n", __func__);
        return -EINVAL;
    }

    return 0;
}

int set_wsa_boost_level(struct audio_device *adev,
                int wsa_num, int boost_table_index)
{
    struct mixer_ctl *ctl;
    const char *mixer_ctl_name_boost_level[] = {
        WSA_LEFT_BOOST_LEVEL,
        WSA_RIGHT_BOOST_LEVEL
    };
    int status = 0;

    ctl = mixer_get_ctl_by_name(adev->mixer,
        mixer_ctl_name_boost_level[wsa_num]);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
                __func__, mixer_ctl_name_boost_level[wsa_num]);
        return -EINVAL;
    }

    status = mixer_ctl_set_value(ctl, 0,
                    spkr_prot_boost_lookup_table[boost_table_index].boost_value);

    if (status < 0) {
        ALOGE("%s: Could not set ctl for mixer %s\n", __func__,
                mixer_ctl_name_boost_level[wsa_num]);
        return -EINVAL;
    }

    return 0;
}

static int spkr_boost_update(struct audio_device *adev,
                unsigned int wsa_num, unsigned int *index, bool spkr2_is_mono)
{
    float dcr = 0;
    unsigned int r0_index = 0, wsa_to_set = wsa_num;
    int boost_max_state = 0;
    int ret = 0;

    /* get R0 value */
    dcr = ((float)handle.sp_r0t0_cal.r0[wsa_num] / MIN_RESISTANCE_SPKR_Q24 * 2);
    if (dcr < MIN_RESISTANCE_LOOKUP) {
        ALOGV("%s: resistance %f changes to min value of 3.2.",
                __func__, dcr);
        dcr = MIN_RESISTANCE_LOOKUP;
    }

    if (dcr > MAX_RESISTANCE_LOOKUP) {
        ALOGV("%s: resistance %f changes to max value of 8.",
                __func__, dcr);
        dcr = MAX_RESISTANCE_LOOKUP;
    }

    r0_index = (int)((dcr - MIN_RESISTANCE_LOOKUP) * 10);
    if (r0_index >= SPKR_PROT_LOOKUP_TABLE_ROWS) {
        ALOGE("%s: r0_index=%d overflows.", __func__, r0_index);
        return -EINVAL;
    }

    boost_max_state = spkr_prot_boost_lookup_table[r0_index].max_state;

    /* In case of wsatz.14 as the only speaker on target, prefix of corresponding
     * mixer ctl in dirver is named SpkrRight. As a result, we have to fixup the
     * WSA number.
     */
    if (spkr2_is_mono)
        wsa_to_set = SP_V2_SPKR_2;

    ret = set_wcd_boost_max_state(adev, boost_max_state, wsa_to_set);
    if (ret < 0) {
        ALOGE("%s: failed to set wcd max boost state.",
            __func__);
        return -EINVAL;
    }

    ret = set_wsa_boost_level(adev, wsa_to_set, r0_index);
    if (ret < 0) {
        ALOGE("%s: failed to set wsa boost level.",
            __func__);
        return -EINVAL;
    }

    *index = r0_index;

    return 0;
}

static void set_boost_and_limiter(struct audio_device *adev,
                unsigned int afe_api_version, enum sp_version sp_prop_version)
{
    int chn = 0;
    int chn_in_use = 0;
    bool wsa_is_8815[SP_V2_NUM_MAX_SPKRS] = {false, false};
    bool spkr2_is_mono_speaker = false;
    unsigned int r0_index = 0;

    /*Do nothing for SPV4.*/
    if(sp_prop_version == SP_V4) {
        handle.sp_version = SP_V4;
        return;
    }
    handle.sp_version = SP_V2;

    /*
     * As long as speaker protection is enabled, WCD and WSA
     * follow lookup table based on R0 impediance regardless
     * of spv2 or spv3.
     */
    check_wsa(adev, vi_feed_no_channels, wsa_is_8815);
    if (vi_feed_no_channels == 1 && wsa_is_8815[SP_V2_SPKR_2])
        spkr2_is_mono_speaker = true;
    /*
     * In case of WSA8815+8810, invalid limiter threshold is sent to DSP
     * for WSA8810 speaker. DSP ignores the invalid value and use default one.
     * The approach let spv3 apply on 8815 and spv2 on 8810 respectively.
     */
    for (chn = 0; chn < vi_feed_no_channels; chn++) {
        chn_in_use = chn;
        if (spkr2_is_mono_speaker)
            chn_in_use = SP_V2_SPKR_2;
        if (wsa_is_8815[chn_in_use] &&
            !spkr_boost_update(adev, chn,
                                    &r0_index, spkr2_is_mono_speaker)) {
            handle.limiter_th[chn] = spv3_limiter_th_q27_table[r0_index];
            handle.sp_version = SP_V3;
        }
        else {
            handle.limiter_th[chn] = DEFAULT_LIMITER_TH;
        }
    }

    /*
     * If spv3 is disabld or ADSP version doesn't comply,
     * ADSP works with SP_V2 version.
     */
    if (sp_prop_version < SP_V3 || afe_api_version < AFE_API_VERSION_SUPPORT_SPV3)
        handle.sp_version = SP_V2;

}

static int spkr_calibrate(int t0_spk_1, int t0_spk_2)
{
    struct audio_device *adev = handle.adev_handle;
    struct audio_cal_info_spk_prot_cfg protCfg;
    struct audio_cal_info_msm_spk_prot_status status;
    int status_v_vali[SP_V2_NUM_MAX_SPKRS], vrms[SP_V2_NUM_MAX_SPKRS];
    bool cleanup = false, disable_rx = false, disable_tx = false;
    int acdb_fd = -1;
    struct audio_usecase *uc_info_rx = NULL, *uc_info_tx = NULL;
    int32_t pcm_dev_rx_id = -1, pcm_dev_tx_id = -1;
    struct timespec ts;
    unsigned long total_time;
    bool acquire_device = false;
    int retry_duration;
    int app_type = 0;
    bool v_validation = false;

    memset(&status, 0, sizeof(status));
    memset(&protCfg, 0, sizeof(protCfg));
    if (!adev) {
        ALOGE("%s: Invalid params", __func__);
        return -EINVAL;
    }
    if (!list_empty(&adev->usecase_list)) {
        ALOGD("%s: Usecase present retry speaker protection", __func__);
        return -EAGAIN;
    }
    if (t0_spk_1 == SPKR_V_VALI_TEMP_MASK &&
        t0_spk_2 == SPKR_V_VALI_TEMP_MASK) {
        ALOGD("%s: v-validation start", __func__);
        v_validation = true;
    }
    acdb_fd = open("/dev/msm_audio_cal",O_RDWR | O_NONBLOCK);
    if (acdb_fd < 0) {
        ALOGE("%s: spkr_prot_thread open msm_acdb failed", __func__);
        return -ENODEV;
    } else {
        protCfg.mode = MSM_SPKR_PROT_CALIBRATION_IN_PROGRESS;
        if (v_validation) {
            if (handle.spkr_prot_mode == MSM_SPKR_PROT_CALIBRATED) {
                t0_spk_1 = handle.sp_r0t0_cal.t0[SP_V2_SPKR_1];
                t0_spk_2 = handle.sp_r0t0_cal.t0[SP_V2_SPKR_2];
            } else {
                t0_spk_1 = SAFE_SPKR_TEMP_Q6;
                t0_spk_2 = SAFE_SPKR_TEMP_Q6;
            }
        }
#ifdef MSM_SPKR_PROT_SPV3
        protCfg.sp_version = handle.sp_version;
#endif
        protCfg.t0[SP_V2_SPKR_1] = t0_spk_1;
        protCfg.t0[SP_V2_SPKR_2] = t0_spk_2;
        if (set_spkr_prot_cal(acdb_fd, &protCfg)) {
            ALOGE("%s: spkr_prot_thread set failed AUDIO_SET_SPEAKER_PROT",
            __func__);
            status.status = -ENODEV;
            goto exit;
        }
    }
    uc_info_rx = (struct audio_usecase *)calloc(1, sizeof(struct audio_usecase));
    if (!uc_info_rx) {
        return -ENOMEM;
    }
    uc_info_rx->id = USECASE_AUDIO_SPKR_CALIB_RX;
    uc_info_rx->type = PCM_PLAYBACK;
    uc_info_rx->in_snd_device = SND_DEVICE_NONE;
    uc_info_rx->stream.out = adev->primary_output;
    list_init(&uc_info_rx->device_list);
    if (fp_audio_extn_is_vbat_enabled())
        uc_info_rx->out_snd_device = SND_DEVICE_OUT_SPEAKER_PROTECTED_VBAT;
    else
        uc_info_rx->out_snd_device = SND_DEVICE_OUT_SPEAKER_PROTECTED;
    disable_rx = true;
    list_add_tail(&adev->usecase_list, &uc_info_rx->list);
    fp_platform_check_and_set_codec_backend_cfg(adev, uc_info_rx,
                                             uc_info_rx->out_snd_device);
    if (fp_audio_extn_is_vbat_enabled())
         fp_enable_snd_device(adev, SND_DEVICE_OUT_SPEAKER_PROTECTED_VBAT);
    else
         fp_enable_snd_device(adev, SND_DEVICE_OUT_SPEAKER_PROTECTED);
    fp_enable_audio_route(adev, uc_info_rx);

    pcm_dev_rx_id = fp_platform_get_pcm_device_id(uc_info_rx->id, PCM_PLAYBACK);
    ALOGV("%s: pcm device id %d", __func__, pcm_dev_rx_id);
    if (pcm_dev_rx_id < 0) {
        ALOGE("%s: Invalid pcm device for usecase (%d)",
              __func__, uc_info_rx->id);
        status.status = -ENODEV;
        goto exit;
    }
    handle.pcm_rx = handle.pcm_tx = NULL;
    handle.pcm_rx = pcm_open(adev->snd_card,
                             pcm_dev_rx_id,
                             PCM_OUT, &pcm_config_skr_prot);
    if (handle.pcm_rx && !pcm_is_ready(handle.pcm_rx)) {
        ALOGE("%s: %s", __func__, pcm_get_error(handle.pcm_rx));
        status.status = -EIO;
        goto exit;
    }
    uc_info_tx = (struct audio_usecase *)
    calloc(1, sizeof(struct audio_usecase));
    if (!uc_info_tx) {
        status.status = -ENOMEM;
        goto exit;
    }
    uc_info_tx->id = USECASE_AUDIO_SPKR_CALIB_TX;
    uc_info_tx->type = PCM_CAPTURE;
    uc_info_tx->in_snd_device = SND_DEVICE_IN_CAPTURE_VI_FEEDBACK;
    uc_info_tx->out_snd_device = SND_DEVICE_NONE;
    list_init(&uc_info_tx->device_list);

    disable_tx = true;
    list_add_tail(&adev->usecase_list, &uc_info_tx->list);
    fp_enable_snd_device(adev, SND_DEVICE_IN_CAPTURE_VI_FEEDBACK);
    fp_enable_audio_route(adev, uc_info_tx);

    pcm_dev_tx_id = fp_platform_get_pcm_device_id(uc_info_tx->id, PCM_CAPTURE);
    ALOGV("%s: pcm device id %d", __func__, pcm_dev_tx_id);
    if (pcm_dev_tx_id < 0) {
        ALOGE("%s: Invalid pcm device for usecase (%d)",
              __func__, uc_info_tx->id);
        status.status = -ENODEV;
        goto exit;
    }
    handle.pcm_tx = pcm_open(adev->snd_card,
                             pcm_dev_tx_id,
                             PCM_IN, &pcm_config_skr_prot);
    if (handle.pcm_tx && !pcm_is_ready(handle.pcm_tx)) {
        ALOGE("%s: %s", __func__, pcm_get_error(handle.pcm_tx));
        status.status = -EIO;
        goto exit;
    }
    if (pcm_start(handle.pcm_rx) < 0) {
        ALOGE("%s: pcm start for RX failed", __func__);
        status.status = -EINVAL;
        goto exit;
    }
    if (pcm_start(handle.pcm_tx) < 0) {
        ALOGE("%s: pcm start for TX failed", __func__);
        status.status = -EINVAL;
        goto exit;
    }
    cleanup = true;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    if (!v_validation) {
        ts.tv_sec += (SLEEP_AFTER_CALIB_START/1000);
    } else {
        total_time = (handle.v_vali_wait_time + handle.v_vali_vali_time);
        ts.tv_sec += (total_time/1000);
        ts.tv_nsec += ((total_time%1000) * 1000000);
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_nsec -= 1000000000;
            ts.tv_sec += 1;
        }
    }
    pthread_mutex_lock(&handle.mutex_spkr_prot);
    pthread_mutex_unlock(&adev->lock);
    acquire_device = true;
    (void)pthread_cond_timedwait(&handle.spkr_calib_cancel,
        &handle.mutex_spkr_prot, &ts);
    ALOGD("%s: Speaker calibration done", __func__);
    cleanup = true;
    pthread_mutex_lock(&handle.spkr_calib_cancelack_mutex);
    if (handle.cancel_spkr_calib) {
        status.status = -EAGAIN;
        goto exit;
    }
    if (acdb_fd > 0) {
        status.status = -EINVAL;
        retry_duration = 0;
        if (v_validation) {
            if (!get_spkr_prot_v_vali_param(acdb_fd, status_v_vali, vrms)) {
                int i;

                for (i = 0; i < vi_feed_no_channels; i++) {
                     if ((status_v_vali[i] != SPKR_V_VALI_SUCCESS)) {
                         ALOGE("%s: failed in v-validation, retry\n", __func__);
                         goto exit;
                     } else {
                         ALOGD("%s: spkr_v_validation success vrms %d",
                         __func__, vrms[i]);
                     }
                }
                status.status = 0;
            }
            goto exit;
        }
        while (!get_spkr_prot_cal(acdb_fd, &status) &&
                retry_duration < GET_SPKR_PROT_CAL_TIMEOUT_MSEC) {
            /*sleep for 200 ms to check for status check*/
            if (!status.status) {
                int i;

                ALOGD("%s: spkr_prot_thread calib Success R0 %d %d",
                 __func__, status.r0[SP_V2_SPKR_1], status.r0[SP_V2_SPKR_2]);
                for (i = 0; i < vi_feed_no_channels; i++) {
                    if (!((status.r0[i] >= MIN_RESISTANCE_SPKR_Q24)
                         && (status.r0[i] < MAX_RESISTANCE_SPKR_Q24))) {
                         ALOGE("%s R0 not in range, retry R0:%d\n", __func__, status.r0[i]);
                         status.status = -EINVAL;
                         break;
                   }
                }
                FILE *fp;
                fp = fopen(CALIB_FILE,"wb");
                if (!fp) {
                    ALOGE("%s: spkr_prot_thread File open failed %s",
                    __func__, strerror(errno));
                    status.status = -ENODEV;
                } else {
                    int i;
                    /* HAL for speaker protection is always calibrating for stereo usecase*/
                    for (i = 0; i < vi_feed_no_channels; i++) {
                        fwrite(&status.r0[i], sizeof(status.r0[i]), 1, fp);
                        fwrite(&protCfg.t0[i], sizeof(protCfg.t0[i]), 1, fp);
                    }
                    fclose(fp);
                }
                break;
            } else if (status.status == -EAGAIN) {
                  ALOGV("%s: spkr_prot_thread try again", __func__);
                  usleep(WAIT_FOR_GET_CALIB_STATUS * 1000);
                  retry_duration += WAIT_FOR_GET_CALIB_STATUS;
            } else {
                ALOGE("%s: spkr_prot_thread get failed status %d",
                __func__, status.status);
                break;
            }
        }
exit:
        if (handle.pcm_rx)
            pcm_close(handle.pcm_rx);
        handle.pcm_rx = NULL;
        if (handle.pcm_tx)
            pcm_close(handle.pcm_tx);
        handle.pcm_tx = NULL;
        if (!v_validation) {
#ifdef MSM_SPKR_PROT_SPV3
            protCfg.sp_version = handle.sp_version;
#endif
            if (!status.status) {
                protCfg.mode = MSM_SPKR_PROT_CALIBRATED;
                protCfg.r0[SP_V2_SPKR_1] = status.r0[SP_V2_SPKR_1];
                protCfg.r0[SP_V2_SPKR_2] = status.r0[SP_V2_SPKR_2];
                if (set_spkr_prot_cal(acdb_fd, &protCfg))
                    ALOGE("%s: spkr_prot_thread disable calib mode", __func__);
                else
                    handle.spkr_prot_mode = MSM_SPKR_PROT_CALIBRATED;
            } else {
                protCfg.mode = MSM_SPKR_PROT_NOT_CALIBRATED;
                handle.spkr_prot_mode = MSM_SPKR_PROT_NOT_CALIBRATED;
                if (set_spkr_prot_cal(acdb_fd, &protCfg))
                    ALOGE("%s: spkr_prot_thread disable calib mode failed", __func__);
            }
        }
        if (acdb_fd > 0)
            close(acdb_fd);

        if (!handle.cancel_spkr_calib && cleanup && !handle.spkr_cal_dynamic) {
            pthread_mutex_unlock(&handle.spkr_calib_cancelack_mutex);
            pthread_cond_wait(&handle.spkr_calib_cancel,
            &handle.mutex_spkr_prot);
            pthread_mutex_lock(&handle.spkr_calib_cancelack_mutex);
        }
        if (disable_rx) {
            list_remove(&uc_info_rx->list);
            if (fp_audio_extn_is_vbat_enabled())
                fp_disable_snd_device(adev, SND_DEVICE_OUT_SPEAKER_PROTECTED_VBAT);
            else
                fp_disable_snd_device(adev, SND_DEVICE_OUT_SPEAKER_PROTECTED);
            fp_disable_audio_route(adev, uc_info_rx);
        }
        if (disable_tx) {
            list_remove(&uc_info_tx->list);
            fp_disable_snd_device(adev, SND_DEVICE_IN_CAPTURE_VI_FEEDBACK);
            fp_disable_audio_route(adev, uc_info_tx);
        }
        if (uc_info_rx) free(uc_info_rx);
        if (uc_info_tx) free(uc_info_tx);
        if (cleanup) {
            if (handle.cancel_spkr_calib)
                pthread_cond_signal(&handle.spkr_calibcancel_ack);
            handle.cancel_spkr_calib = 0;
            pthread_mutex_unlock(&handle.spkr_calib_cancelack_mutex);
            pthread_mutex_unlock(&handle.mutex_spkr_prot);
        }
    }
    if (acquire_device)
        pthread_mutex_lock(&adev->lock);
    return status.status;
}

static void spkr_calibrate_wait()
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += WAKEUP_MIN_IDLE_CHECK;
    pthread_mutex_lock(&handle.cal_wait_cond_mutex);
    pthread_cond_timedwait(&handle.cal_wait_condition,
                           &handle.cal_wait_cond_mutex, &ts);
    pthread_mutex_unlock(&handle.cal_wait_cond_mutex);
}

static void* spkr_calibration_thread()
{
    unsigned long sec = 0;
    int t0;
    int t0_spk_1 = 0;
    int t0_spk_2 = 0;
    bool goahead = false;
    struct audio_cal_info_spk_prot_cfg protCfg;
    FILE *fp;
    int acdb_fd, thermal_fd;
    struct audio_device *adev = handle.adev_handle;
    unsigned long min_idle_time = MIN_SPKR_IDLE_SEC;
    char value[PROPERTY_VALUE_MAX];
    char afe_version_value[PROPERTY_VALUE_MAX];
    char wsa_path[MAX_PATH] = {0};
    int spk_1_tzn, spk_2_tzn;
    char buf[32] = {0};
    int ret;
    bool spv3_enable = false;
    bool spv4_enable = false;
    enum sp_version sp_prop_version = 0;
    unsigned int afe_api_version = 0;
    struct mixer_ctl *ctl;

    memset(&protCfg, 0, sizeof(protCfg));
    /* If the value of this persist.vendor.audio.spkr.cal.duration is 0
     * then it means it will take 30min to calibrate
     * and if the value is greater than zero then it would take
     * that much amount of time to calibrate.
     */
    property_get("persist.vendor.audio.spkr.cal.duration", value, "0");
    if (atoi(value) > 0)
        min_idle_time = atoi(value);
    else {
        property_get("persist.spkr.cal.duration", value, "0");
        if (atoi(value) > 0)
            min_idle_time = atoi(value);
    }
    handle.speaker_prot_threadid = pthread_self();
    spv3_enable = property_get_bool("persist.vendor.audio.spv3.enable", false);
    property_get("persist.vendor.audio.avs.afe_api_version", afe_version_value,
                 "0");
    if (atoi(afe_version_value) > 0)
        afe_api_version = atoi(afe_version_value);

    spv4_enable = property_get_bool("persist.vendor.audio.spv4.enable", false);
    if (spv4_enable)
        sp_prop_version = SP_V4;
    else if (spv3_enable)
        sp_prop_version = SP_V3;

    if(spv4_enable)
        handle.sp_version = SP_V4;
    ALOGD("spkr_prot_thread enable prot Entryi sp_version %d", handle.sp_version);

    acdb_fd = open("/dev/msm_audio_cal",O_RDWR | O_NONBLOCK);
    if (acdb_fd > 0) {
        /*Set processing mode with t0/r0*/
        protCfg.mode = MSM_SPKR_PROT_NOT_CALIBRATED;
#ifdef MSM_SPKR_PROT_SPV3
        protCfg.sp_version = handle.sp_version;
#endif
        if (set_spkr_prot_cal(acdb_fd, &protCfg)) {
            ALOGE("%s: spkr_prot_thread enable prot failed", __func__);
            handle.spkr_prot_mode = MSM_SPKR_PROT_DISABLED;
            close(acdb_fd);
        } else
            handle.spkr_prot_mode = MSM_SPKR_PROT_NOT_CALIBRATED;
    } else {
        handle.spkr_prot_mode = MSM_SPKR_PROT_DISABLED;
        ALOGE("%s: Failed to open acdb node", __func__);
    }
    if (handle.spkr_prot_mode == MSM_SPKR_PROT_DISABLED) {
        ALOGD("%s: Speaker protection disabled", __func__);
        pthread_exit(0);
        return NULL;
    }

    if (!handle.spkr_cal_dynamic || handle.apply_cal) {
        bool spkr_calibrated = false;
        fp = fopen(CALIB_FILE,"rb");
        if (fp) {
            int i;
            spkr_calibrated = true;
            for (i = 0; i < vi_feed_no_channels; i++) {
                 fread(&protCfg.r0[i], sizeof(protCfg.r0[i]), 1, fp);
                 fread(&protCfg.t0[i], sizeof(protCfg.t0[i]), 1, fp);
            }
            ALOGD("%s: spkr_prot_thread r0 value %d %d",
                  __func__, protCfg.r0[SP_V2_SPKR_1], protCfg.r0[SP_V2_SPKR_2]);
            ALOGD("%s: spkr_prot_thread t0 value %d %d",
                   __func__, protCfg.t0[SP_V2_SPKR_1], protCfg.t0[SP_V2_SPKR_2]);
            fclose(fp);
            /*Valid tempature range: -30C to 80C(in q6 format)
              Valid Resistance range: 2 ohms to 40 ohms(in q24 format)*/
            for (i = 0; i < vi_feed_no_channels; i++) {
                 if (!((protCfg.t0[i] > MIN_SPKR_TEMP_Q6) && (protCfg.t0[i] < MAX_SPKR_TEMP_Q6)
                     && (protCfg.r0[i] >= MIN_RESISTANCE_SPKR_Q24)
                     && (protCfg.r0[i] < MAX_RESISTANCE_SPKR_Q24))) {
                     spkr_calibrated = false;
                     break;
                 }
            }
            if (spkr_calibrated) {
                ALOGD("%s: Spkr calibrated", __func__);
                protCfg.mode = MSM_SPKR_PROT_CALIBRATED;
#ifdef MSM_SPKR_PROT_SPV3
                protCfg.sp_version = handle.sp_version;
#endif
                if (set_spkr_prot_cal(acdb_fd, &protCfg)) {
                    ALOGE("%s: enable prot failed", __func__);
                    handle.spkr_prot_mode = MSM_SPKR_PROT_DISABLED;
                } else
                    handle.spkr_prot_mode = MSM_SPKR_PROT_CALIBRATED;

                set_boost_and_limiter(adev, afe_api_version, sp_prop_version);
            }
        }
        if (handle.spkr_cal_dynamic || spkr_calibrated) {
            close(acdb_fd);
            handle.apply_cal = false;
            pthread_exit(0);
            return NULL;
        }
    }
    if (acdb_fd > 0)
        close(acdb_fd);

    ALOGV("%s: start calibration", __func__);
    while (!handle.thread_exit) {
        if (handle.wsa_found) {
            if (!handle.is_wsa_temp_mixer_ctl) {
                spk_1_tzn = handle.spkr_1_tzn;
                spk_2_tzn = handle.spkr_2_tzn;
            }
            goahead = false;
            pthread_mutex_lock(&adev->lock);
            if (is_speaker_in_use(&sec)) {
                ALOGV("%s: WSA Speaker in use retry calibration", __func__);
                pthread_mutex_unlock(&adev->lock);
                spkr_calibrate_wait();
                continue;
            } else {
                ALOGD("%s: wsa speaker idle %ld,minimum time %ld", __func__, sec, min_idle_time);
                if (!adev->primary_output ||
                    ((sec < min_idle_time) && !handle.trigger_cal)) {
                    pthread_mutex_unlock(&adev->lock);
                    spkr_calibrate_wait();
                    continue;
               }
               goahead = true;
           }
           if (!list_empty(&adev->usecase_list)) {
                ALOGD("%s: Usecase active re-try calibration", __func__);
                pthread_mutex_unlock(&adev->lock);
                spkr_calibrate_wait();
                continue;
           }
           if (goahead) {
               if (handle.is_wsa_temp_mixer_ctl) {
                   ret = spkr_get_temp(adev, WSA_SPKR_LEFT, &t0_spk_1);
                   if (!ret) {
                       if (t0_spk_1 < TZ_TEMP_MIN_THRESHOLD ||
                           t0_spk_1 > TZ_TEMP_MAX_THRESHOLD) {
                           pthread_mutex_unlock(&adev->lock);
                           spkr_calibrate_wait();
                           continue;
                       }
                       ALOGD("%s: temp T0 for spkr1 %d\n", __func__, t0_spk_1);
                       /*Convert temp into q6 format*/
                       t0_spk_1 = (t0_spk_1 * (1 << 6));
                   }
                   ret = spkr_get_temp(adev, WSA_SPKR_RIGHT, &t0_spk_2);
                   if (!ret) {
                       if (t0_spk_2 < TZ_TEMP_MIN_THRESHOLD ||
                           t0_spk_2 > TZ_TEMP_MAX_THRESHOLD) {
                           pthread_mutex_unlock(&adev->lock);
                           spkr_calibrate_wait();
                           continue;
                       }
                       ALOGD("%s: temp T0 for spkr2 %d\n", __func__, t0_spk_2);
                       /*Convert temp into q6 format*/
                       t0_spk_2 = (t0_spk_2 * (1 << 6));
                   }
               } else {
                   if (spk_1_tzn >= 0) {
                       const char *mixer_ctl_name = "SpkrLeft WSA T0 Init";
                       snprintf(wsa_path, MAX_PATH, TZ_WSA, spk_1_tzn);
                       ALOGV("%s: wsa_path: %s\n", __func__, wsa_path);
                       thermal_fd = -1;

                       ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
                       if (ctl) {
                           ALOGD("%s: Got ctl for mixer cmd %s",
                                 __func__, mixer_ctl_name);
                            mixer_ctl_set_value(ctl, 0, 1);
                        }
                       thermal_fd = open(wsa_path, O_RDONLY);
                       if (thermal_fd > 0) {
                           if ((ret = read(thermal_fd, buf, sizeof(buf))) >= 0)
                               t0_spk_1 = atoi(buf);
                           else
                               ALOGE("%s: read fail for %s err:%d\n",
                                     __func__, wsa_path, ret);
                            close(thermal_fd);
                       } else {
                           ALOGE("%s: fd for %s is NULL\n", __func__, wsa_path);
                       }
                       if (ctl) {
                           mixer_ctl_set_value(ctl, 0, 0);
                       }
                       if (t0_spk_1 < TZ_TEMP_MIN_THRESHOLD ||
                           t0_spk_1 > TZ_TEMP_MAX_THRESHOLD) {
                           pthread_mutex_unlock(&adev->lock);
                           spkr_calibrate_wait();
                           continue;
                       }
                       ALOGD("%s: temp T0 for spkr1 %d\n", __func__, t0_spk_1);
                       /*Convert temp into q6 format*/
                       t0_spk_1 = (t0_spk_1 * (1 << 6));
                   }
                   if (spk_2_tzn >= 0) {
                       const char *mixer_ctl_name = "SpkrRight WSA T0 Init";
                       snprintf(wsa_path, MAX_PATH, TZ_WSA, spk_2_tzn);
                       ALOGV("%s: wsa_path: %s\n", __func__, wsa_path);
                       ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
                       if (ctl) {
                           ALOGD("%s: Got ctl for mixer cmd %s",
                                     __func__, mixer_ctl_name);
                           mixer_ctl_set_value(ctl, 0, 1);
                        }
                        thermal_fd = open(wsa_path, O_RDONLY);
                        if (thermal_fd > 0) {
                           if ((ret = read(thermal_fd, buf, sizeof(buf))) >= 0)
                               t0_spk_2 = atoi(buf);
                           else
                               ALOGE("%s: read fail for %s err:%d\n",
                                     __func__, wsa_path, ret);
                           close(thermal_fd);
                        } else {
                           ALOGE("%s: fd for %s is NULL\n", __func__, wsa_path);
                        }
                        if (ctl) {
                           mixer_ctl_set_value(ctl, 0, 0);
                        }
                        if (t0_spk_2 < TZ_TEMP_MIN_THRESHOLD ||
                           t0_spk_2 > TZ_TEMP_MAX_THRESHOLD) {
                           pthread_mutex_unlock(&adev->lock);
                           spkr_calibrate_wait();
                           continue;
                        }
                        ALOGD("%s: temp T0 for spkr2 %d\n", __func__, t0_spk_2);
                        /*Convert temp into q6 format*/
                        t0_spk_2 = (t0_spk_2 * (1 << 6));
                   }
               }
           }
           pthread_mutex_unlock(&adev->lock);
        } else if (!handle.thermal_client_request("spkr",1)) {
            ALOGD("%s: wait for callback from thermal daemon", __func__);
            pthread_mutex_lock(&handle.spkr_prot_thermalsync_mutex);
            pthread_cond_wait(&handle.spkr_prot_thermalsync,
            &handle.spkr_prot_thermalsync_mutex);
            /*Convert temp into q6 format*/
            t0 = (handle.spkr_prot_t0 * (1 << 6));
            pthread_mutex_unlock(&handle.spkr_prot_thermalsync_mutex);
            if (t0 < MIN_SPKR_TEMP_Q6 || t0 > MAX_SPKR_TEMP_Q6) {
                ALOGE("%s: Calibration temparature error %d", __func__,
                      handle.spkr_prot_t0);
                continue;
            }
            t0_spk_1 = t0;
            t0_spk_2 = t0;
            ALOGD("%s: Request t0 success value %d", __func__,
            handle.spkr_prot_t0);
        } else {
            ALOGE("%s: Request t0 failed", __func__);
            /*Assume safe value for temparature*/
            t0_spk_1 = SAFE_SPKR_TEMP_Q6;
            t0_spk_2 = SAFE_SPKR_TEMP_Q6;
        }
        goahead = false;
        pthread_mutex_lock(&adev->lock);
        if (is_speaker_in_use(&sec)) {
            ALOGV("%s: Speaker in use retry calibration", __func__);
            pthread_mutex_unlock(&adev->lock);
            spkr_calibrate_wait();
            continue;
        } else {
            if (!(sec > min_idle_time || handle.trigger_cal)) {
                pthread_mutex_unlock(&adev->lock);
                spkr_calibrate_wait();
                continue;
            }
            goahead = true;
        }
        if (!list_empty(&adev->usecase_list)) {
            ALOGD("%s: Usecase active re-try calibration", __func__);
            goahead = false;
            pthread_mutex_unlock(&adev->lock);
            spkr_calibrate_wait();
            continue;
        }
        if (goahead) {
                int status;
                /* DSP always calibrates 1st channel data in mono case.
                 * When wsatz14 is the only speaker on target, temperature
                 * sensor data comes in 2nd channel. Therefore, we have to swap
                 * sensor channel to fix the mismatch.
                 */
                if (handle.is_wsa_temp_mixer_ctl) {
                    if (!handle.is_spkr1_avail && handle.is_spkr1_avail)
                        status = spkr_calibrate(t0_spk_2, t0_spk_1);
                    else
                        status = spkr_calibrate(t0_spk_1, t0_spk_2);
                } else {
                    if ( handle.spkr_1_tzn <= 0 && handle.spkr_2_tzn > 0)
                         status = spkr_calibrate(t0_spk_2, t0_spk_1);
                    else
                         status = spkr_calibrate(t0_spk_1, t0_spk_2);
                }
                pthread_mutex_unlock(&adev->lock);
                if (status == -EAGAIN) {
                    ALOGE("%s: failed to calibrate try again %s",
                    __func__, strerror(status));
                    continue;
                } else {
                    ALOGE("%s: calibrate status %s", __func__, strerror(status));
                }
                ALOGD("%s: spkr_prot_thread end calibration", __func__);
                handle.trigger_cal = false;
                break;
        }
    }
    if (handle.thermal_client_handle)
        handle.thermal_client_unregister_callback(handle.thermal_client_handle);
    handle.thermal_client_handle = 0;
    if (handle.thermal_handle)
        dlclose(handle.thermal_handle);
    handle.thermal_handle = NULL;

    set_boost_and_limiter(adev, afe_api_version, sp_prop_version);

    pthread_exit(0);
    return NULL;
}

static int thermal_client_callback(int temp)
{
    pthread_mutex_lock(&handle.spkr_prot_thermalsync_mutex);
    ALOGD("%s: spkr_prot set t0 %d and signal", __func__, temp);
    if (handle.spkr_prot_mode == MSM_SPKR_PROT_NOT_CALIBRATED)
        handle.spkr_prot_t0 = temp;
    pthread_cond_signal(&handle.spkr_prot_thermalsync);
    pthread_mutex_unlock(&handle.spkr_prot_thermalsync_mutex);
    return 0;
}

void spkr_prot_set_parameters(struct str_parms *parms,
                                         char *value, int len)
{
    int err;

    if (property_get_bool("vendor.audio.read.wsatz.type", false)) {
        if ((!tz_names.spkr_2_name) && (strstr(value, "wsa")))
            tz_names.spkr_2_name = strdup(value);
        else if ((!tz_names.spkr_1_name) && (strstr(value, "wsa")))
            tz_names.spkr_1_name = strdup(value);
    } else {

        err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_SPKR_TZ_1,
                            value, len);
        if (err >= 0) {
            tz_names.spkr_1_name = strdup(value);
            str_parms_del(parms, AUDIO_PARAMETER_KEY_SPKR_TZ_1);
        }

        err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_SPKR_TZ_2,
                            value, len);
        if (err >= 0) {
            tz_names.spkr_2_name = strdup(value);
            str_parms_del(parms, AUDIO_PARAMETER_KEY_SPKR_TZ_2);
        }
    }
    ALOGV("%s: tz1: %s, tz2: %s", __func__,
          tz_names.spkr_1_name, tz_names.spkr_2_name);
}

static int spkr_vi_channels(struct audio_device *adev)
{
    int vi_channels, vi_channel_num_by_wsa = 0;
    int temp = 0, ret = 0;

    vi_channels = vi_feed_get_channels(adev);
    ALOGD("%s: vi_channels %d", __func__, vi_channels);
    if (vi_channels < 0 || vi_channels > SP_V2_NUM_MAX_SPKRS) {
        /* limit the number of channels to SP_V2_NUM_MAX_SPKRS */
        vi_channels = SP_V2_NUM_MAX_SPKRS;
    }

    ret = spkr_get_temp(adev, WSA_SPKR_LEFT, &temp);
    if (!ret) {
        vi_channel_num_by_wsa++;
        handle.is_spkr1_avail = true;
    }
    ret = spkr_get_temp(adev, WSA_SPKR_RIGHT, &temp);
    if (!ret) {
        vi_channel_num_by_wsa++;
        handle.is_spkr2_avail = true;
    }

    if (handle.is_spkr1_avail || handle.is_spkr2_avail) {
        handle.wsa_found = true;
        handle.is_wsa_temp_mixer_ctl = true;
    } else {
        ALOGD("%s: tz1: %s, tz2: %s", __func__,
               tz_names.spkr_1_name, tz_names.spkr_2_name);
        handle.spkr_1_tzn = get_tzn(tz_names.spkr_1_name);
        handle.spkr_2_tzn = get_tzn(tz_names.spkr_2_name);
        /* Update VI channel number by WSA number */
        if (handle.spkr_1_tzn >= 0)
            vi_channel_num_by_wsa++;

        if (handle.spkr_2_tzn >= 0)
            vi_channel_num_by_wsa++;

         if (vi_channel_num_by_wsa > 0)
            handle.wsa_found = true;
    }

    if (vi_channel_num_by_wsa < vi_channels)
            vi_channels = vi_channel_num_by_wsa;

    return vi_channels;
}

static void get_spkr_prot_thermal_cal(char *param)
{
    int i, status = 0;
    int r0[SP_V2_NUM_MAX_SPKRS] = {0}, t0[SP_V2_NUM_MAX_SPKRS] = {0};
    double dr0[SP_V2_NUM_MAX_SPKRS] = {0}, dt0[SP_V2_NUM_MAX_SPKRS] = {0};

    FILE *fp = fopen(CALIB_FILE,"rb");
    if (fp) {
        for (i = 0; i < vi_feed_no_channels; i++) {
            fread(&r0[i], sizeof(int), 1, fp);
            fread(&t0[i], sizeof(int), 1, fp);
            /* Convert from ADSP format to readable format */
            dr0[i] = ((double)r0[i])/(1 << 24);
            dt0[i] = ((double)t0[i])/(1 << 6);
        }
        ALOGV("%s: R0= %lf, %lf, T0= %lf, %lf",
              __func__, dr0[0], dr0[1], dt0[0], dt0[1]);
        fclose(fp);
    } else {
        ALOGE("%s: failed to open cal file\n", __func__);
        status = -EINVAL;
    }
    snprintf(param, MAX_STR_SIZE - strlen(param) - 1,
            "SpkrCalStatus: %d; R0: %lf, %lf; T0: %lf, %lf",
            status, dr0[SP_V2_SPKR_1], dr0[SP_V2_SPKR_2],
            dt0[SP_V2_SPKR_1], dt0[SP_V2_SPKR_2]);
    ALOGD("%s:: param = %s\n", __func__, param);

    return;
}

#ifdef MSM_SPKR_PROT_IN_FTM_MODE

static int set_spkr_prot_ftm_cfg(int wait_time, int ftm_time)
{
    int ret = 0;
    struct audio_cal_sp_th_vi_ftm_cfg th_cal_data;
    struct audio_cal_sp_ex_vi_ftm_cfg ex_cal_data;

    int cal_fd = open("/dev/msm_audio_cal",O_RDWR | O_NONBLOCK);
    if (cal_fd < 0) {
        ALOGE("%s: open msm_acdb failed", __func__);
        ret = -ENODEV;
        goto done;
    }

    memset(&th_cal_data, 0, sizeof(th_cal_data));
    th_cal_data.hdr.data_size = sizeof(th_cal_data);
    th_cal_data.hdr.version = VERSION_0_0;
    th_cal_data.hdr.cal_type = AFE_FB_SPKR_PROT_TH_VI_CAL_TYPE;
    th_cal_data.hdr.cal_type_size = sizeof(th_cal_data.cal_type);
    th_cal_data.cal_type.cal_hdr.version = VERSION_0_0;
    th_cal_data.cal_type.cal_hdr.buffer_number = 0;
    th_cal_data.cal_type.cal_info.wait_time[SP_V2_SPKR_1] = wait_time;
    th_cal_data.cal_type.cal_info.wait_time[SP_V2_SPKR_2] = wait_time;
    th_cal_data.cal_type.cal_info.ftm_time[SP_V2_SPKR_1] = ftm_time;
    th_cal_data.cal_type.cal_info.ftm_time[SP_V2_SPKR_2] = ftm_time;
    th_cal_data.cal_type.cal_info.mode = MSM_SPKR_PROT_IN_FTM_MODE; // FTM mode
    th_cal_data.cal_type.cal_data.mem_handle = -1;

    if (ioctl(cal_fd, AUDIO_SET_CALIBRATION, &th_cal_data))
        ALOGE("%s: failed to set TH VI FTM_CFG, errno = %d", __func__, errno);

    memset(&ex_cal_data, 0, sizeof(ex_cal_data));
    ex_cal_data.hdr.data_size = sizeof(ex_cal_data);
    ex_cal_data.hdr.version = VERSION_0_0;
    ex_cal_data.hdr.cal_type = AFE_FB_SPKR_PROT_EX_VI_CAL_TYPE;
    ex_cal_data.hdr.cal_type_size = sizeof(ex_cal_data.cal_type);
    ex_cal_data.cal_type.cal_hdr.version = VERSION_0_0;
    ex_cal_data.cal_type.cal_hdr.buffer_number = 0;
    ex_cal_data.cal_type.cal_info.wait_time[SP_V2_SPKR_1] = wait_time;
    ex_cal_data.cal_type.cal_info.wait_time[SP_V2_SPKR_2] = wait_time;
    ex_cal_data.cal_type.cal_info.ftm_time[SP_V2_SPKR_1] = ftm_time;
    ex_cal_data.cal_type.cal_info.ftm_time[SP_V2_SPKR_2] = ftm_time;
    ex_cal_data.cal_type.cal_info.mode = MSM_SPKR_PROT_IN_FTM_MODE; // FTM mode
    ex_cal_data.cal_type.cal_data.mem_handle = -1;

    if (ioctl(cal_fd, AUDIO_SET_CALIBRATION, &ex_cal_data))
        ALOGE("%s: failed to set EX VI FTM_CFG, ret = %d", __func__, errno);

    if (cal_fd > 0)
        close(cal_fd);
done:
    return ret;
}

static void get_spkr_prot_ftm_param(char *param)
{
    struct audio_cal_sp_th_vi_param th_vi_cal_data;
    struct audio_cal_sp_ex_vi_param ex_vi_cal_data;
#ifdef MSM_SPKR_PROT_SPV4
    struct audio_cal_sp_v4_ex_vi_param spv4_ex_vi_cal_data;
    double re[SP_V2_NUM_MAX_SPKRS] = {0}, Bl[SP_V2_NUM_MAX_SPKRS] = {0};
    double rms[SP_V2_NUM_MAX_SPKRS] = {0}, kms[SP_V2_NUM_MAX_SPKRS] = {0};
    double fre[SP_V2_NUM_MAX_SPKRS] = {0}, qms[SP_V2_NUM_MAX_SPKRS] = {0};
#endif
    int i;
    int ftm_status[SP_V2_NUM_MAX_SPKRS] = {0};
    int ex_vi_status[SP_V2_NUM_MAX_SPKRS] = {0};
    double rdc[SP_V2_NUM_MAX_SPKRS] = {0}, temp[SP_V2_NUM_MAX_SPKRS] = {0};
    double f[SP_V2_NUM_MAX_SPKRS] = {0}, r[SP_V2_NUM_MAX_SPKRS] = {0}, q[SP_V2_NUM_MAX_SPKRS] = {0};

    int cal_fd = open("/dev/msm_audio_cal",O_RDWR | O_NONBLOCK);
    if (cal_fd < 0) {
        ALOGE("%s: open msm_acdb failed", __func__);
        goto done;
    }

    memset(&th_vi_cal_data, 0, sizeof(th_vi_cal_data));
    th_vi_cal_data.cal_type.cal_info.status[SP_V2_SPKR_1] = -EINVAL;
    th_vi_cal_data.cal_type.cal_info.status[SP_V2_SPKR_2] = -EINVAL;
    th_vi_cal_data.hdr.data_size = sizeof(th_vi_cal_data);
    th_vi_cal_data.hdr.version = VERSION_0_0;
    th_vi_cal_data.hdr.cal_type = AFE_FB_SPKR_PROT_TH_VI_CAL_TYPE;
    th_vi_cal_data.hdr.cal_type_size = sizeof(th_vi_cal_data.cal_type);
    th_vi_cal_data.cal_type.cal_hdr.version = VERSION_0_0;
    th_vi_cal_data.cal_type.cal_hdr.buffer_number = 0;
    th_vi_cal_data.cal_type.cal_data.mem_handle = -1;
#ifdef MSM_SPKR_PROT_IN_V_VALI_MODE
    /* for v-validation, same cal type is used.
     * need this mode info to differentiate feature under test */
    th_vi_cal_data.cal_type.cal_info.mode = MSM_SPKR_PROT_IN_FTM_MODE; // FTM mode
#endif

    if (ioctl(cal_fd, AUDIO_GET_CALIBRATION, &th_vi_cal_data))
        ALOGE("%s: Error %d in getting th_vi_cal_data", __func__, errno);

   if (handle.sp_version == SP_V4) {
#ifdef MSM_SPKR_PROT_SPV4
        memset(&spv4_ex_vi_cal_data, 0, sizeof(spv4_ex_vi_cal_data));
        spv4_ex_vi_cal_data.cal_type.cal_info.status[SP_V2_SPKR_1] = -EINVAL;
        spv4_ex_vi_cal_data.cal_type.cal_info.status[SP_V2_SPKR_2] = -EINVAL;
        spv4_ex_vi_cal_data.hdr.data_size = sizeof(spv4_ex_vi_cal_data);
        spv4_ex_vi_cal_data.hdr.version = VERSION_0_0;
        spv4_ex_vi_cal_data.hdr.cal_type = AFE_FB_SPKR_PROT_V4_EX_VI_CAL_TYPE;
        spv4_ex_vi_cal_data.hdr.cal_type_size = sizeof(spv4_ex_vi_cal_data.cal_type);
        spv4_ex_vi_cal_data.cal_type.cal_hdr.version = VERSION_0_0;
        spv4_ex_vi_cal_data.cal_type.cal_hdr.buffer_number = 0;
        spv4_ex_vi_cal_data.cal_type.cal_data.mem_handle = -1;

        if (ioctl(cal_fd, AUDIO_GET_CALIBRATION, &spv4_ex_vi_cal_data))
            ALOGE("%s: Error %d in getting spv4_ex_vi_cal_data", __func__, errno);
#endif
    } else {
        memset(&ex_vi_cal_data, 0, sizeof(ex_vi_cal_data));
        ex_vi_cal_data.cal_type.cal_info.status[SP_V2_SPKR_1] = -EINVAL;
        ex_vi_cal_data.cal_type.cal_info.status[SP_V2_SPKR_2] = -EINVAL;
        ex_vi_cal_data.hdr.data_size = sizeof(ex_vi_cal_data);
        ex_vi_cal_data.hdr.version = VERSION_0_0;
        ex_vi_cal_data.hdr.cal_type = AFE_FB_SPKR_PROT_EX_VI_CAL_TYPE;
        ex_vi_cal_data.hdr.cal_type_size = sizeof(ex_vi_cal_data.cal_type);
        ex_vi_cal_data.cal_type.cal_hdr.version = VERSION_0_0;
        ex_vi_cal_data.cal_type.cal_hdr.buffer_number = 0;
        ex_vi_cal_data.cal_type.cal_data.mem_handle = -1;

        if (ioctl(cal_fd, AUDIO_GET_CALIBRATION, &ex_vi_cal_data))
            ALOGE("%s: Error %d in getting ex_vi_cal_data", __func__, errno);
    }

    for (i = 0; i < vi_feed_no_channels; i++) {
        /* Convert from ADSP format to readable format */
        rdc[i] = ((double)th_vi_cal_data.cal_type.cal_info.r_dc_q24[i])/(1<<24);
        temp[i] = ((double)th_vi_cal_data.cal_type.cal_info.temp_q22[i])/(1<<22);

       if (handle.sp_version == SP_V4) {
#ifdef MSM_SPKR_PROT_SPV4
            re[i] = ((double)spv4_ex_vi_cal_data.cal_type.cal_info.ftm_re_q24[i])/(1<<24);
            Bl[i] = ((double)spv4_ex_vi_cal_data.cal_type.cal_info.ftm_Bl_q24[i])/(1<<24);
            rms[i] = ((double)spv4_ex_vi_cal_data.cal_type.cal_info.ftm_Rms_q24[i])/(1<<24);
            kms[i] = ((double)spv4_ex_vi_cal_data.cal_type.cal_info.ftm_Kms_q24[i])/(1<<24);
            fre[i] = ((double)spv4_ex_vi_cal_data.cal_type.cal_info.ftm_freq_q20[i])/(1<<20);
            qms[i] = ((double)spv4_ex_vi_cal_data.cal_type.cal_info.ftm_Qms_q24[i])/(1<<24);
            ex_vi_status[i] = spv4_ex_vi_cal_data.cal_type.cal_info.status[i];
#endif
        } else {
            f[i] = ((double)ex_vi_cal_data.cal_type.cal_info.freq_q20[i])/(1<<20);
            r[i] = ((double)ex_vi_cal_data.cal_type.cal_info.resis_q24[i])/(1<<24);
            q[i] = ((double)ex_vi_cal_data.cal_type.cal_info.qmct_q24[i])/(1<<24);
            ex_vi_status[i] = ex_vi_cal_data.cal_type.cal_info.status[i];
        }

        if (th_vi_cal_data.cal_type.cal_info.status[i] == 0 &&
            ex_vi_status[i] == 0) {
            ftm_status[i] = 0;
        } else if (th_vi_cal_data.cal_type.cal_info.status[i] == -EAGAIN &&
                   ex_vi_status[i] == -EAGAIN) {
            ftm_status[i] = -EAGAIN;
        } else {
            ftm_status[i] = -EINVAL;
        }
    }

   if (handle.sp_version == SP_V4) {
#ifdef MSM_SPKR_PROT_SPV4
        snprintf(param, MAX_STR_SIZE - strlen(param) - 1,
            "SpkrParamStatus: %d, %d; Rdc: %lf, %lf; Temp: %lf, %lf;"
            " Res: %lf, %lf; Bl: %lf, %lf; Rms: %lf, %lf;"
            " Kms: %lf, %lf; Fres: %lf, %lf; Qms: %lf, %lf",
            ftm_status[SP_V2_SPKR_1], ftm_status[SP_V2_SPKR_2],
            rdc[SP_V2_SPKR_1], rdc[SP_V2_SPKR_2], temp[SP_V2_SPKR_1],
            temp[SP_V2_SPKR_2], re[SP_V2_SPKR_1], re[SP_V2_SPKR_2],
            Bl[SP_V2_SPKR_1], Bl[SP_V2_SPKR_2], rms[SP_V2_SPKR_1], rms[SP_V2_SPKR_2],
            kms[SP_V2_SPKR_1], kms[SP_V2_SPKR_2],
            fre[SP_V2_SPKR_1], fre[SP_V2_SPKR_2], qms[SP_V2_SPKR_1], qms[SP_V2_SPKR_2]);
#endif
    } else {
        snprintf(param, MAX_STR_SIZE - strlen(param) - 1,
            "SpkrParamStatus: %d, %d; Rdc: %lf, %lf; Temp: %lf, %lf;"
            " Freq: %lf, %lf; Rect: %lf, %lf; Qmct: %lf, %lf",
            ftm_status[SP_V2_SPKR_1], ftm_status[SP_V2_SPKR_2],
            rdc[SP_V2_SPKR_1], rdc[SP_V2_SPKR_2], temp[SP_V2_SPKR_1],
            temp[SP_V2_SPKR_2], f[SP_V2_SPKR_1], f[SP_V2_SPKR_2],
            r[SP_V2_SPKR_1], r[SP_V2_SPKR_2], q[SP_V2_SPKR_1], q[SP_V2_SPKR_2]);
    }

    if (cal_fd > 0)
        close(cal_fd);
done:
    return;
}

#else

static void get_spkr_prot_ftm_param(char *param __unused)
{

    ALOGD("%s: not supported", __func__);
    return;
}

static int set_spkr_prot_ftm_cfg(int wait_time __unused, int ftm_time __unused)
{
    ALOGD("%s: not supported", __func__);
    return -ENOSYS;
}
#endif

#ifdef MSM_SPKR_PROT_IN_V_VALI_MODE

static int set_spkr_prot_v_vali_cfg(int wait_time, int vali_time)
{
    int ret = 0;
    struct audio_cal_sp_th_vi_v_vali_cfg cal_data;

    int cal_fd = open("/dev/msm_audio_cal",O_RDWR | O_NONBLOCK);
    if (cal_fd < 0) {
        ALOGE("%s: open msm_acdb failed", __func__);
        ret = -ENODEV;
        goto done;
    }

    memset(&cal_data, 0, sizeof(cal_data));
    cal_data.hdr.data_size = sizeof(cal_data);
    cal_data.hdr.version = VERSION_0_0;
    cal_data.hdr.cal_type = AFE_FB_SPKR_PROT_TH_VI_CAL_TYPE;
    cal_data.hdr.cal_type_size = sizeof(cal_data.cal_type);
    cal_data.cal_type.cal_hdr.version = VERSION_0_0;
    cal_data.cal_type.cal_hdr.buffer_number = 0;
    cal_data.cal_type.cal_info.wait_time[SP_V2_SPKR_1] = wait_time;
    cal_data.cal_type.cal_info.wait_time[SP_V2_SPKR_2] = wait_time;
    cal_data.cal_type.cal_info.vali_time[SP_V2_SPKR_1] = vali_time;
    cal_data.cal_type.cal_info.vali_time[SP_V2_SPKR_2] = vali_time;
    cal_data.cal_type.cal_info.mode = MSM_SPKR_PROT_IN_V_VALI_MODE; // V-VALI mode
    cal_data.cal_type.cal_data.mem_handle = -1;
    handle.v_vali_wait_time = wait_time;
    handle.v_vali_vali_time = vali_time;

    if (ioctl(cal_fd, AUDIO_SET_CALIBRATION, &cal_data))
        ALOGE("%s: failed to set TH VI V_VALI_CFG, errno = %d", __func__, errno);

    if (cal_fd > 0)
        close(cal_fd);
done:
    return ret;
}

static int get_spkr_prot_v_vali_param(int cal_fd, int *status, int *vrms)
{
    struct audio_cal_sp_th_vi_v_vali_param cal_data;
    int ret = 0;

    if (cal_fd < 0) {
        ALOGE("%s: Error: cal_fd = %d", __func__, cal_fd);
        ret = -EINVAL;
        goto done;
    }

    if (status == NULL || vrms == NULL) {
        ALOGE("%s: Error: status or vrms NULL", __func__);
        ret = -EINVAL;
        goto done;
    }

    memset(&cal_data, 0, sizeof(cal_data));
    cal_data.cal_type.cal_info.status[SP_V2_SPKR_1] = -EINVAL;
    cal_data.cal_type.cal_info.status[SP_V2_SPKR_2] = -EINVAL;
    cal_data.hdr.data_size = sizeof(cal_data);
    cal_data.hdr.version = VERSION_0_0;
    cal_data.hdr.cal_type = AFE_FB_SPKR_PROT_TH_VI_CAL_TYPE;
    cal_data.hdr.cal_type_size = sizeof(cal_data.cal_type);
    cal_data.cal_type.cal_hdr.version = VERSION_0_0;
    cal_data.cal_type.cal_hdr.buffer_number = 0;
    cal_data.cal_type.cal_data.mem_handle = -1;
    cal_data.cal_type.cal_info.mode = MSM_SPKR_PROT_IN_V_VALI_MODE; // V-VALI mode

    if (ioctl(cal_fd, AUDIO_GET_CALIBRATION, &cal_data)) {
        ALOGE("%s: Error %d in getting V-VALI cal_data", __func__, errno);
        ret = -ENODEV;
        goto done;
    }

    ALOGD("%s:: vrms = %d %d, status = %d %d\n", __func__,
          cal_data.cal_type.cal_info.vrms_q24[SP_V2_SPKR_1],
          cal_data.cal_type.cal_info.vrms_q24[SP_V2_SPKR_2],
          cal_data.cal_type.cal_info.status[SP_V2_SPKR_1],
          cal_data.cal_type.cal_info.status[SP_V2_SPKR_2]);

    vrms[SP_V2_SPKR_1] = cal_data.cal_type.cal_info.vrms_q24[SP_V2_SPKR_1];
    vrms[SP_V2_SPKR_2] = cal_data.cal_type.cal_info.vrms_q24[SP_V2_SPKR_2];
    status[SP_V2_SPKR_1] = cal_data.cal_type.cal_info.status[SP_V2_SPKR_1];
    status[SP_V2_SPKR_2] = cal_data.cal_type.cal_info.status[SP_V2_SPKR_2];

done:
    return ret;
}
#else

static int set_spkr_prot_v_vali_cfg(int wait_time __unused, int vali_time __unused)
{
    ALOGD("%s: not supported", __func__);
    return -ENOSYS;
}

static int get_spkr_prot_v_vali_param(int cal_fd __unused, int *status __unused,
                                      int *vrms __unused)
{
    ALOGD("%s: not supported", __func__);
    return -ENOSYS;
}
#endif

static void* spkr_v_vali_thread()
{
    int ret = 0;
    struct audio_device *adev = handle.adev_handle;
    handle.v_vali_threadid = pthread_self();

    if (!handle.v_vali_wait_time)
        handle.v_vali_wait_time = SPKR_V_VALI_DEFAULT_WAIT_TIME;/*set default if not setparam */
    if (!handle.v_vali_vali_time)
        handle.v_vali_vali_time = SPKR_V_VALI_DEFAULT_VALI_TIME;/*set default if not setparam */
    set_spkr_prot_v_vali_cfg(handle.v_vali_wait_time, handle.v_vali_vali_time);
    pthread_mutex_lock(&adev->lock);
    ret = spkr_calibrate(SPKR_V_VALI_TEMP_MASK,
                         SPKR_V_VALI_TEMP_MASK);/*use 0xfffe as temp to initiate v_vali*/
    pthread_mutex_unlock(&adev->lock);
    if (ret)
        ALOGE("%s: failed, retry again\n", __func__);
    handle.trigger_v_vali = false;
    pthread_exit(0);
    return NULL;
}

static void spkr_calibrate_signal()
{
    pthread_mutex_lock(&handle.cal_wait_cond_mutex);
    pthread_cond_signal(&handle.cal_wait_condition);
    pthread_mutex_unlock(&handle.cal_wait_cond_mutex);
}

static void spkr_calib_thread_create()
{
    int result = 0;

    if (!handle.spkr_prot_enable) {
        ALOGD("%s: Speaker protection disabled", __func__);
        return;
    }
    if (handle.cal_thrd_created) {
           result = pthread_join(handle.spkr_calibration_thread, (void **) NULL);
           if (result < 0) {
               ALOGE("%s:Unable to join the calibration thread", __func__);
               return;
           }
           handle.cal_thrd_created = false;
    }

    result = pthread_create(&handle.spkr_calibration_thread,
               (const pthread_attr_t *) NULL, spkr_calibration_thread, &handle);
    if (result == 0) {
        handle.cal_thrd_created = true;
    } else {
        ALOGE("%s: speaker calibration thread creation failed", __func__);
        handle.trigger_cal = false;
    }
}

static void spkr_v_vali_thread_create()
{
    int result = 0;

    if (!handle.spkr_prot_enable) {
        ALOGD("%s: Speaker protection disabled", __func__);
        return;
    }
    if (handle.v_vali_thrd_created) {
        result = pthread_join(handle.spkr_v_vali_thread, (void **) NULL);
        if (result < 0) {
            ALOGE("%s:Unable to join the v-vali thread", __func__);
            return;
        }
        handle.v_vali_thrd_created = false;
    }
    result = pthread_create(&handle.spkr_v_vali_thread,
               (const pthread_attr_t *) NULL, spkr_v_vali_thread, &handle);
    if (result == 0) {
        handle.v_vali_thrd_created = true;
    } else {
        ALOGE("%s: failed to create v_vali thread\n", __func__);
        handle.trigger_v_vali = false;
    }
}

static bool fbsp_parms_allowed(struct str_parms *parms)
{
    if (str_parms_has_key(parms, AUDIO_PARAMETER_KEY_FBSP_TRIGGER_SPKR_CAL))
        return true;
    if (str_parms_has_key(parms, AUDIO_PARAMETER_KEY_FBSP_APPLY_SPKR_CAL))
        return true;
    if (str_parms_has_key(parms, AUDIO_PARAMETER_KEY_FBSP_GET_SPKR_CAL))
        return true;
    if (str_parms_has_key(parms, AUDIO_PARAMETER_KEY_FBSP_CFG_WAIT_TIME))
        return true;
    if (str_parms_has_key(parms, AUDIO_PARAMETER_KEY_FBSP_CFG_FTM_TIME))
        return true;
    if (str_parms_has_key(parms, AUDIO_PARAMETER_KEY_FBSP_GET_FTM_PARAM))
        return true;
    if (str_parms_has_key(parms, AUDIO_PARAMETER_KEY_FBSP_TRIGGER_V_VALI))
        return true;
    if (str_parms_has_key(parms, AUDIO_PARAMETER_KEY_FBSP_V_VALI_WAIT_TIME))
        return true;
    if (str_parms_has_key(parms, AUDIO_PARAMETER_KEY_FBSP_V_VALI_VALI_TIME))
        return true;

    return false;
}

int fbsp_set_parameters(struct str_parms *parms)
{
    int ret= 0 , err;
    char *value = NULL;
    int len;
    char *test_r = NULL;
    char *cfg_str;
    int wait_time, ftm_time, vali_time;
    char *kv_pairs = str_parms_to_str(parms);

    if(kv_pairs == NULL) {
        ret = -ENOMEM;
        ALOGE("[%s] key-value pair is NULL",__func__);
        goto done;
    }
    ALOGV_IF(kv_pairs != NULL, "%s: enter: %s", __func__, kv_pairs);

    if (!fbsp_parms_allowed(parms)) {
        ret = -EINVAL;
        goto done;
    }

    len = strlen(kv_pairs);
    value = (char*)calloc(len, sizeof(char));
    if (value == NULL) {
        ret = -ENOMEM;
        ALOGE("[%s] failed to allocate memory",__func__);
        goto done;
    }
    if (!handle.spkr_prot_enable) {
        ALOGD("%s: Speaker protection disabled", __func__);
        goto done;
    }

    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FBSP_TRIGGER_SPKR_CAL, value,
                            len);
    if (err >= 0) {
        str_parms_del(parms, AUDIO_PARAMETER_KEY_FBSP_TRIGGER_SPKR_CAL);
        if ((strcmp(value, "true") == 0) || (strcmp(value, "yes") == 0)) {
            if (handle.trigger_cal)
                goto done;
            handle.trigger_cal = true;
            spkr_calibrate_signal();
            if (handle.spkr_cal_dynamic)
                spkr_calib_thread_create();
        }
        goto done;
    }
    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FBSP_APPLY_SPKR_CAL, value,
                            len);
    if (err >= 0) {
        str_parms_del(parms, AUDIO_PARAMETER_KEY_FBSP_APPLY_SPKR_CAL);
        if ((strcmp(value, "true") == 0) || (strcmp(value, "yes") == 0)) {
            if (handle.apply_cal)
                goto done;
            handle.apply_cal = true;
            if (handle.spkr_cal_dynamic)
                spkr_calib_thread_create();
        }
        goto done;
    }

    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FBSP_TRIGGER_V_VALI, value,
                            len);
    if (err >= 0) {
        str_parms_del(parms, AUDIO_PARAMETER_KEY_FBSP_TRIGGER_V_VALI);
        if ((strcmp(value, "true") == 0) || (strcmp(value, "yes") == 0)) {
            if (handle.trigger_v_vali)
                goto done;
            handle.trigger_v_vali = true;
            spkr_v_vali_thread_create();
        }
        goto done;
    }
    /* Expected key value pair is in below format:
     * AUDIO_PARAM_FBSP_CFG_WAIT_TIME=waittime;AUDIO_PARAM_FBSP_CFG_FTM_TIME=ftmtime;
     * Parse waittime and ftmtime from it.
     */
    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FBSP_CFG_WAIT_TIME,
                            value, len);
    if (err >= 0) {
        str_parms_del(parms, AUDIO_PARAMETER_KEY_FBSP_CFG_WAIT_TIME);
        cfg_str = strtok_r(value, ";", &test_r);
        if (cfg_str == NULL) {
            ALOGE("%s: incorrect wait time cfg_str", __func__);
            ret = -EINVAL;
            goto done;
        }
        wait_time = atoi(cfg_str);
        ALOGV(" %s: cfg_str = %s, wait_time = %d", __func__, cfg_str, wait_time);

        err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FBSP_CFG_FTM_TIME,
                                value, len);
        if (err >= 0) {
            str_parms_del(parms, AUDIO_PARAMETER_KEY_FBSP_CFG_FTM_TIME);
            cfg_str = strtok_r(value, ";", &test_r);
            if (cfg_str == NULL) {
                ALOGE("%s: incorrect ftm time cfg_str", __func__);
                ret = -EINVAL;
                goto done;
            }
            ftm_time = atoi(cfg_str);
            ALOGV(" %s: cfg_str = %s, ftm_time = %d", __func__, cfg_str, ftm_time);

            ret = set_spkr_prot_ftm_cfg(wait_time, ftm_time);
            if (ret < 0) {
                ALOGE("%s: set_spkr_prot_ftm_cfg failed", __func__);
                goto done;
            }
        }
    }
    /* Expected key value pair is in below format:
     * AUDIO_PARAM_FBSP_V_VALI_WAIT_TIME=waittime;AUDIO_PARAM_FBSP_V_VALI_VALI_TIME=valitime;
     * Parse waittime and validationtime from it.
     */
    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FBSP_V_VALI_WAIT_TIME,
                            value, len);
    if (err >= 0) {
        str_parms_del(parms, AUDIO_PARAMETER_KEY_FBSP_V_VALI_WAIT_TIME);
        cfg_str = strtok_r(value, ";", &test_r);
        if (cfg_str == NULL) {
            ALOGE("%s: incorrect wait time cfg_str", __func__);
            ret = -EINVAL;
            goto done;
        }
        wait_time = atoi(cfg_str);
        ALOGV(" %s: cfg_str = %s, wait_time = %d", __func__, cfg_str, wait_time);

        err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FBSP_V_VALI_VALI_TIME,
                                value, len);
        if (err >= 0) {
            str_parms_del(parms, AUDIO_PARAMETER_KEY_FBSP_V_VALI_VALI_TIME);
            cfg_str = strtok_r(value, ";", &test_r);
            if (cfg_str == NULL) {
                ALOGE("%s: incorrect validation time cfg_str", __func__);
                ret = -EINVAL;
                goto done;
            }
            vali_time = atoi(cfg_str);
            ALOGV(" %s: cfg_str = %s, vali_time = %d", __func__, cfg_str, vali_time);

            ret = set_spkr_prot_v_vali_cfg(wait_time, vali_time);
            if (ret < 0) {
                ALOGE("%s: set_spkr_prot_v_vali_cfg failed", __func__);
                goto done;
            }
        }
    }

done:
    ALOGV("%s: exit with code(%d)", __func__, ret);

    if(kv_pairs != NULL)
        free(kv_pairs);
    if(value != NULL)
        free(value);

    return ret;
}

int fbsp_get_parameters(struct str_parms *query,
                                   struct str_parms *reply)
{
    int err = 0;
    char value[MAX_STR_SIZE] = {0};

    if (!handle.spkr_prot_enable) {
        ALOGD("%s: Speaker protection disabled", __func__);
        return -EINVAL;
    }

    err = str_parms_get_str(query, AUDIO_PARAMETER_KEY_FBSP_GET_SPKR_CAL, value,
                                                          sizeof(value));
    if (err >= 0) {
        get_spkr_prot_thermal_cal(value);
        str_parms_add_str(reply, AUDIO_PARAMETER_KEY_FBSP_GET_SPKR_CAL, value);
    }
    err = str_parms_get_str(query, AUDIO_PARAMETER_KEY_FBSP_GET_FTM_PARAM, value,
                            sizeof(value));
    if (err >= 0) {
        get_spkr_prot_ftm_param(value);
        str_parms_add_str(reply, AUDIO_PARAMETER_KEY_FBSP_GET_FTM_PARAM, value);
    }
    return err;
}

void spkr_prot_init(void *adev, spkr_prot_init_config_t spkr_prot_init_config_val)
{
    char value[PROPERTY_VALUE_MAX];
    int result = 0;
    pthread_condattr_t attr;
    ALOGD("%s: Initialize speaker protection module", __func__);
    memset(&handle, 0, sizeof(handle));
    if (!adev) {
        ALOGE("%s: Invalid params", __func__);
        return;
    }
    handle.spkr_prot_enable = false;
    handle.thread_exit = false;
    handle.cal_thrd_created = false;
    if ((property_get("persist.vendor.audio.speaker.prot.enable",
                      value, NULL) > 0)) {
        if (!strncmp("true", value, 4))
             handle.spkr_prot_enable = true;
    }
    if (!handle.spkr_prot_enable) {
        ALOGD("%s: Speaker protection disabled", __func__);
        return;
    }
    handle.spkr_cal_dynamic = property_get_bool("persist.vendor.audio.spkr.cal.dynamic", false);
    // init function pointers
    fp_read_line_from_file = spkr_prot_init_config_val.fp_read_line_from_file;
    fp_get_usecase_from_list =  spkr_prot_init_config_val.fp_get_usecase_from_list;
    fp_disable_snd_device = spkr_prot_init_config_val.fp_disable_snd_device;
    fp_enable_snd_device = spkr_prot_init_config_val.fp_enable_snd_device;
    fp_disable_audio_route = spkr_prot_init_config_val.fp_disable_audio_route;
    fp_enable_audio_route = spkr_prot_init_config_val.fp_enable_audio_route;
    fp_platform_set_snd_device_backend = spkr_prot_init_config_val.fp_platform_set_snd_device_backend;
    fp_platform_get_snd_device_name_extn = spkr_prot_init_config_val.fp_platform_get_snd_device_name_extn;
    fp_platform_get_default_app_type_v2 = spkr_prot_init_config_val.fp_platform_get_default_app_type_v2;
    fp_platform_send_audio_calibration = spkr_prot_init_config_val.fp_platform_send_audio_calibration;
    fp_platform_get_pcm_device_id = spkr_prot_init_config_val.fp_platform_get_pcm_device_id;
    fp_platform_get_snd_device_name = spkr_prot_init_config_val.fp_platform_get_snd_device_name;
    fp_platform_spkr_prot_is_wsa_analog_mode = spkr_prot_init_config_val.fp_platform_spkr_prot_is_wsa_analog_mode;
    fp_platform_get_vi_feedback_snd_device = spkr_prot_init_config_val.fp_platform_get_vi_feedback_snd_device;
    fp_platform_get_spkr_prot_snd_device = spkr_prot_init_config_val.fp_platform_get_spkr_prot_snd_device;
    fp_platform_check_and_set_codec_backend_cfg = spkr_prot_init_config_val.fp_platform_check_and_set_codec_backend_cfg;
    fp_audio_extn_is_vbat_enabled = spkr_prot_init_config_val.fp_audio_extn_is_vbat_enabled;
    handle.adev_handle = adev;
    handle.spkr_prot_mode = MSM_SPKR_PROT_DISABLED;
    handle.spkr_processing_state = SPKR_PROCESSING_IN_IDLE;
    handle.spkr_prot_t0 = -1;
    handle.trigger_cal = false;
    /* HAL for speaker protection is always calibrating for stereo usecase*/
    vi_feed_no_channels = spkr_vi_channels(adev);
    if (vi_feed_no_channels < 0) {
        ALOGE("%s: no of channels negative !!", __func__);
        /* limit the number of channels to 2*/
        vi_feed_no_channels = 2;
    }

    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&handle.cal_wait_condition, &attr);
    pthread_mutex_init(&handle.cal_wait_cond_mutex, NULL);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    if (handle.wsa_found) {
        if (fp_platform_spkr_prot_is_wsa_analog_mode(adev) == 1) {
            ALOGD("%s: WSA analog mode", __func__);
            pcm_config_skr_prot.channels = WSA_ANALOG_MODE_CHANNELS;
        }
        pthread_cond_init(&handle.spkr_calib_cancel, &attr);
        pthread_cond_init(&handle.spkr_calibcancel_ack, NULL);
        pthread_mutex_init(&handle.mutex_spkr_prot, NULL);
        pthread_mutex_init(&handle.spkr_calib_cancelack_mutex, NULL);
        if (!handle.spkr_cal_dynamic) {
            ALOGD("%s:WSA Create calibration thread", __func__);
            spkr_calib_thread_create();
        }
    return;
    } else {
        ALOGD("%s: WSA spkr calibration thread is not created", __func__);
    }
    pthread_cond_init(&handle.spkr_prot_thermalsync, NULL);
    pthread_cond_init(&handle.spkr_calib_cancel, &attr);
    pthread_cond_init(&handle.spkr_calibcancel_ack, NULL);
    pthread_mutex_init(&handle.mutex_spkr_prot, NULL);
    pthread_mutex_init(&handle.spkr_calib_cancelack_mutex, NULL);
    pthread_mutex_init(&handle.spkr_prot_thermalsync_mutex, NULL);
    handle.thermal_handle = dlopen("/vendor/lib/libthermalclient.so",
            RTLD_NOW);
    if (!handle.thermal_handle) {
        ALOGE("%s: DLOPEN for thermal client failed", __func__);
    } else {
        /*Query callback function symbol*/
        handle.client_register_callback =
       (int (*)(char *, int (*)(int),void *))
        dlsym(handle.thermal_handle, "thermal_client_register_callback");
        handle.thermal_client_unregister_callback =
        (void (*)(int) )
        dlsym(handle.thermal_handle, "thermal_client_unregister_callback");
        if (!handle.client_register_callback ||
            !handle.thermal_client_unregister_callback) {
            ALOGE("%s: DLSYM thermal_client_register_callback failed", __func__);
        } else {
            /*Register callback function*/
            handle.thermal_client_handle =
            handle.client_register_callback("spkr", thermal_client_callback, NULL);
            if (!handle.thermal_client_handle) {
                ALOGE("%s: client_register_callback failed", __func__);
            } else {
                ALOGD("%s: spkr_prot client_register_callback success", __func__);
                handle.thermal_client_request = (int (*)(char *, int))
                dlsym(handle.thermal_handle, "thermal_client_request");
            }
        }
    }
    if (handle.thermal_client_request) {
        ALOGD("%s: Create calibration thread", __func__);
        result = pthread_create(&handle.spkr_calibration_thread,
        (const pthread_attr_t *) NULL, spkr_calibration_thread, &handle);
        if (result == 0) {
            handle.cal_thrd_created = true;
        } else {
            ALOGE("%s: speaker calibration thread creation failed", __func__);
            destroy_thread_params();
        }
    } else {
        ALOGE("%s: thermal_client_request failed", __func__);
        if (handle.thermal_client_handle &&
            handle.thermal_client_unregister_callback)
            handle.thermal_client_unregister_callback(handle.thermal_client_handle);
        if (handle.thermal_handle)
            dlclose(handle.thermal_handle);
        handle.thermal_handle = NULL;
        handle.spkr_prot_enable = false;
    }

    if (handle.spkr_prot_enable) {
        char platform[PROPERTY_VALUE_MAX];
        property_get("ro.board.platform", platform, "");
        if (!strncmp("apq8084", platform, sizeof("apq8084"))) {
            fp_platform_set_snd_device_backend(SND_DEVICE_OUT_VOICE_SPEAKER,
                                            "speaker-protected",
                                            "SLIMBUS_0_RX");
        }
    }
}

int spkr_prot_deinit()
{
    int result = 0;

    ALOGD("%s: Entering deinit cal_thrd_created :%d",
          __func__, handle.cal_thrd_created);

    handle.thread_exit = true;
    spkr_calibrate_signal();
    if (handle.cal_thrd_created) {
        result = pthread_join(handle.spkr_calibration_thread,
                              (void **) NULL);
        if (result < 0) {
            ALOGE("%s:Unable to join the calibration thread", __func__);
            return -1;
        }
        handle.cal_thrd_created = false;
    }
    if (handle.v_vali_thrd_created) {
        result = pthread_join(handle.spkr_v_vali_thread,
                              (void **) NULL);
        if (result < 0) {
            ALOGE("%s:Unable to join the v_vali thread", __func__);
            return -1;
        }
        handle.v_vali_thrd_created = false;
    }
    destroy_thread_params();
    memset(&handle, 0, sizeof(handle));
    return 0;
}

int select_spkr_prot_cal_data(snd_device_t snd_device)
{
    struct audio_cal_info_spk_prot_cfg protCfg;
    int acdb_fd = -1;
    int ret = 0;

    acdb_fd = open("/dev/msm_audio_cal", O_RDWR | O_NONBLOCK);
    if (acdb_fd < 0) {
        ALOGE("%s: open msm_acdb failed", __func__);
        return -ENODEV;
    }
    switch(snd_device) {
        case SND_DEVICE_OUT_VOICE_SPEAKER_2_PROTECTED_VBAT:
        case SND_DEVICE_OUT_VOICE_SPEAKER_2_PROTECTED:
            protCfg.r0[SP_V2_SPKR_1] = handle.sp_r0t0_cal.r0[SP_V2_SPKR_2];
            protCfg.r0[SP_V2_SPKR_2] = handle.sp_r0t0_cal.r0[SP_V2_SPKR_1];
            protCfg.t0[SP_V2_SPKR_1] = handle.sp_r0t0_cal.t0[SP_V2_SPKR_2];
            protCfg.t0[SP_V2_SPKR_2] = handle.sp_r0t0_cal.t0[SP_V2_SPKR_1];
            break;
        default:
            protCfg.r0[SP_V2_SPKR_1] = handle.sp_r0t0_cal.r0[SP_V2_SPKR_1];
            protCfg.r0[SP_V2_SPKR_2] = handle.sp_r0t0_cal.r0[SP_V2_SPKR_2];
            protCfg.t0[SP_V2_SPKR_1] = handle.sp_r0t0_cal.t0[SP_V2_SPKR_1];
            protCfg.t0[SP_V2_SPKR_2] = handle.sp_r0t0_cal.t0[SP_V2_SPKR_2];
            break;
    }
    protCfg.mode = MSM_SPKR_PROT_CALIBRATED;
#ifdef MSM_SPKR_PROT_SPV3
    protCfg.sp_version = handle.sp_version;
    protCfg.limiter_th[SP_V2_SPKR_1] = handle.limiter_th[SP_V2_SPKR_1];
    protCfg.limiter_th[SP_V2_SPKR_2] = handle.limiter_th[SP_V2_SPKR_2];
#endif
    ret = set_spkr_prot_cal(acdb_fd, &protCfg);
    if (ret)
        ALOGE("%s: speaker protection cal data swap failed", __func__);

    close(acdb_fd);
    return ret;
}

int spkr_prot_start_processing(snd_device_t snd_device)
{
    struct audio_usecase *uc_info_tx;
    struct audio_device *adev = handle.adev_handle;
    int32_t pcm_dev_tx_id = -1, ret = 0;
    snd_device_t in_snd_device;
    char device_name[DEVICE_NAME_MAX_SIZE] = {0};
    int app_type = 0;

    ALOGV("%s: Entry", __func__);
    /* cancel speaker calibration */
    if (!adev) {
       ALOGE("%s: Invalid params", __func__);
       return -EINVAL;
    }
    snd_device = fp_platform_get_spkr_prot_snd_device(snd_device);
    if (handle.spkr_prot_mode == MSM_SPKR_PROT_CALIBRATED) {
        ret = select_spkr_prot_cal_data(snd_device);
        if (ret) {
            ALOGE("%s: Setting speaker protection cal data failed", __func__);
            return ret;
        }
    }

    in_snd_device = fp_platform_get_vi_feedback_snd_device(snd_device);
    spkr_prot_set_spkrstatus(true);
    uc_info_tx = (struct audio_usecase *)calloc(1, sizeof(struct audio_usecase));
    if (!uc_info_tx) {
        return -ENOMEM;
    }
    uc_info_tx->id = USECASE_AUDIO_SPKR_CALIB_TX;
    uc_info_tx->type = PCM_CAPTURE;
    list_init(&uc_info_tx->device_list);

    if (fp_platform_get_snd_device_name_extn(adev->platform, snd_device, device_name) < 0) {
        ALOGE("%s: Invalid sound device returned", __func__);
        return -EINVAL;
    }
    ALOGD("%s: spkr snd_device(%d: %s)", __func__, snd_device,
           device_name);
    audio_route_apply_and_update_path(adev->audio_route,
           device_name);

    pthread_mutex_lock(&handle.mutex_spkr_prot);
    if (handle.spkr_processing_state == SPKR_PROCESSING_IN_IDLE) {
        uc_info_tx->in_snd_device = in_snd_device;
        uc_info_tx->out_snd_device = SND_DEVICE_NONE;
        handle.pcm_tx = NULL;
        list_add_tail(&adev->usecase_list, &uc_info_tx->list);
        fp_enable_snd_device(adev, in_snd_device);
        fp_enable_audio_route(adev, uc_info_tx);

        pcm_dev_tx_id = fp_platform_get_pcm_device_id(uc_info_tx->id, PCM_CAPTURE);
        if (pcm_dev_tx_id < 0) {
            ALOGE("%s: Invalid pcm device for usecase (%d)",
                  __func__, uc_info_tx->id);
            ret = -ENODEV;
            goto exit;
        }
        handle.pcm_tx = pcm_open(adev->snd_card,
                                 pcm_dev_tx_id,
                                 PCM_IN, &pcm_config_skr_prot);
        if (handle.pcm_tx && !pcm_is_ready(handle.pcm_tx)) {
            ALOGE("%s: %s", __func__, pcm_get_error(handle.pcm_tx));
            ret = -EIO;
            goto exit;
        }
        if (pcm_start(handle.pcm_tx) < 0) {
            ALOGE("%s: pcm start for TX failed", __func__);
            ret = -EINVAL;
        }
    }

exit:
    if (ret) {
        if (handle.pcm_tx)
            pcm_close(handle.pcm_tx);
        handle.pcm_tx = NULL;
        list_remove(&uc_info_tx->list);
        uc_info_tx->in_snd_device = in_snd_device;
        uc_info_tx->out_snd_device = SND_DEVICE_NONE;
        fp_disable_snd_device(adev, in_snd_device);
        fp_disable_audio_route(adev, uc_info_tx);
        free(uc_info_tx);
    } else
        handle.spkr_processing_state = SPKR_PROCESSING_IN_PROGRESS;
    pthread_mutex_unlock(&handle.mutex_spkr_prot);
    ALOGV("%s: Exit", __func__);
    return ret;
}

void spkr_prot_stop_processing(snd_device_t snd_device)
{
    struct audio_usecase *uc_info_tx;
    struct audio_device *adev = handle.adev_handle;
    snd_device_t in_snd_device;

    ALOGV("%s: Entry", __func__);
    snd_device = fp_platform_get_spkr_prot_snd_device(snd_device);
    spkr_prot_set_spkrstatus(false);
    in_snd_device = fp_platform_get_vi_feedback_snd_device(snd_device);

    pthread_mutex_lock(&handle.mutex_spkr_prot);
    if (adev && handle.spkr_processing_state == SPKR_PROCESSING_IN_PROGRESS) {
        uc_info_tx = fp_get_usecase_from_list(adev, USECASE_AUDIO_SPKR_CALIB_TX);
        if (handle.pcm_tx)
            pcm_close(handle.pcm_tx);
        handle.pcm_tx = NULL;
        fp_disable_snd_device(adev, in_snd_device);
        if (uc_info_tx) {
            list_remove(&uc_info_tx->list);
            fp_disable_audio_route(adev, uc_info_tx);
            free(uc_info_tx);
        }
    }
    handle.spkr_processing_state = SPKR_PROCESSING_IN_IDLE;
    pthread_mutex_unlock(&handle.mutex_spkr_prot);
    if (adev)
        audio_route_reset_and_update_path(adev->audio_route,
                                      fp_platform_get_snd_device_name(snd_device));
    ALOGV("%s: Exit", __func__);
}

bool spkr_prot_is_enabled()
{
    return handle.spkr_prot_enable;
}

void spkr_prot_is_enabled_init()
{

}

#endif /*SPKR_PROT_ENABLED*/
