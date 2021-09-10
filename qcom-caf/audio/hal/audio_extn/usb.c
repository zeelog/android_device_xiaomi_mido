/*
 * Copyright (c) 2013, 2016-2020, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "audio_hw_usb"
#define LOG_NDEBUG 0
#define LOG_NDDEBUG 0

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <log/log.h>
#include <cutils/str_parms.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <system/audio.h>
#include <tinyalsa/asoundlib.h>
#include <audio_hw.h>
#include <cutils/properties.h>
#include <ctype.h>
#include <math.h>
#include <unistd.h>
#include "audio_extn.h"

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_USB
#include <log_utils.h>
#endif

#define USB_BUFF_SIZE           2048
#define CHANNEL_NUMBER_STR      "Channels: "
#define PLAYBACK_PROFILE_STR    "Playback:"
#define CAPTURE_PROFILE_STR     "Capture:"
#define DATA_PACKET_INTERVAL_STR "Data packet interval:"
#define USB_SIDETONE_GAIN_STR   "usb_sidetone_gain"
#define ABS_SUB(A, B) (((A) > (B)) ? ((A) - (B)):((B) - (A)))
#define SAMPLE_RATE_8000          8000
#define SAMPLE_RATE_11025         11025
#define SAMPLE_RATE_192000        192000
// Supported sample rates for USB
#define USBID_SIZE                16
static uint32_t supported_sample_rates[] =
    {384000, 352800, 192000, 176400, 96000, 88200, 64000, 48000, 44100, 32000, 22050, 16000, 11025, 8000};
static uint32_t supported_sample_rates_mask[2];

#define  MAX_SAMPLE_RATE_SIZE  sizeof(supported_sample_rates)/sizeof(supported_sample_rates[0])

#define DEFAULT_SERVICE_INTERVAL_US    0

#define _MAX(x, y) (((x) >= (y)) ? (x) : (y))
#define _MIN(x, y) (((x) <= (y)) ? (x) : (y))

typedef enum usb_usecase_type{
    USB_PLAYBACK = 0,
    USB_CAPTURE,
} usb_usecase_type_t;

enum {
    USB_SIDETONE_ENABLE_INDEX = 0,
    USB_SIDETONE_VOLUME_INDEX,
    USB_SIDETONE_MAX_INDEX,
};

struct usb_device_config {
    struct listnode list;
    unsigned int bit_width;
    unsigned int channels;
    unsigned int rate_size;
    unsigned int rates[MAX_SAMPLE_RATE_SIZE];
    unsigned long service_interval_us;
    usb_usecase_type_t type;
};

struct usb_card_config {
    struct listnode list;
    audio_devices_t usb_device_type;
    int usb_card;
    struct listnode usb_device_conf_list;
    struct mixer *usb_snd_mixer;
    int usb_sidetone_index[USB_SIDETONE_MAX_INDEX];
    int usb_sidetone_vol_min;
    int usb_sidetone_vol_max;
    int endian;
    char usbid[USBID_SIZE];
};

struct usb_module {
    struct listnode usb_card_conf_list;
    struct audio_device *adev;
    int sidetone_gain;
    bool is_capture_supported;
    bool usb_reconfig;
};

static struct usb_module *usbmod = NULL;
static bool usb_audio_debug_enable = false;
static int usb_sidetone_gain = 0;

static const char * const usb_sidetone_enable_str[] = {
    "Sidetone Playback Switch",
    "Mic Playback Switch",
};

static const char * const usb_sidetone_volume_str[] = {
    "Sidetone Playback Volume",
    "Mic Playback Volume",
};

static int usb_get_sidetone_gain(struct usb_card_config *card_info)
{
    int gain = card_info->usb_sidetone_vol_min + usbmod->sidetone_gain;
    if (gain > card_info->usb_sidetone_vol_max)
        gain = card_info->usb_sidetone_vol_max;
    return gain;
}

static void usb_get_sidetone_volume(struct usb_card_config *usb_card_info)
{
    struct mixer_ctl *ctl;
    unsigned int index;

    if (!audio_extn_usb_is_sidetone_volume_enabled())
        return;

    for (index = 0;
         index < sizeof(usb_sidetone_volume_str)/sizeof(usb_sidetone_volume_str[0]);
         index++) {
        ctl = mixer_get_ctl_by_name(usb_card_info->usb_snd_mixer,
                                    usb_sidetone_volume_str[index]);
        if (ctl) {
            usb_card_info->usb_sidetone_index[USB_SIDETONE_VOLUME_INDEX] = index;
            usb_card_info->usb_sidetone_vol_min = mixer_ctl_get_range_min(ctl);
            usb_card_info->usb_sidetone_vol_max = mixer_ctl_get_range_max(ctl);
            break;
        }
    }
}

static void usb_set_sidetone_volume(struct usb_card_config *usb_card_info,
                                        bool enable, int index)
{
    struct mixer_ctl *ctl;

    if (!audio_extn_usb_is_sidetone_volume_enabled())
        return;

    ctl = mixer_get_ctl_by_name(usb_card_info->usb_snd_mixer,
                                usb_sidetone_volume_str[index]);

    if (ctl == NULL)
        ALOGV("%s: sidetone gain mixer command is not found",
              __func__);
    else if (enable)
        mixer_ctl_set_value(ctl, 0,
                            usb_get_sidetone_gain(usb_card_info));
}



static void usb_mixer_print_enum(struct mixer_ctl *ctl)
{
    unsigned int num_enums;
    unsigned int i;
    const char *string;

    num_enums = mixer_ctl_get_num_enums(ctl);

    for (i = 0; i < num_enums; i++) {
        string = mixer_ctl_get_enum_string(ctl, i);
        ALOGI("\t%s%s", mixer_ctl_get_value(ctl, 0) == (int)i ? ">" : "", string);
    }
}

static void usb_soundcard_detail_control(struct mixer *mixer, const char *control)
{
    struct mixer_ctl *ctl;
    enum mixer_ctl_type type;
    unsigned int num_values;
    unsigned int i;
    int min, max;

    if (isdigit(control[0]))
        ctl = mixer_get_ctl(mixer, atoi(control));
    else
        ctl = mixer_get_ctl_by_name(mixer, control);

    if (!ctl) {
        fprintf(stderr, "Invalid mixer control\n");
        return;
    }

    type = mixer_ctl_get_type(ctl);
    num_values = mixer_ctl_get_num_values(ctl);

    ALOGI("%s:", mixer_ctl_get_name(ctl));

    for (i = 0; i < num_values; i++) {
        switch (type) {
            case MIXER_CTL_TYPE_INT:
                ALOGI(" %d", mixer_ctl_get_value(ctl, i));
                break;
            case MIXER_CTL_TYPE_BOOL:
                ALOGI(" %s", mixer_ctl_get_value(ctl, i) ? "On" : "Off");
                break;
            case MIXER_CTL_TYPE_ENUM:
                usb_mixer_print_enum(ctl);
                break;
            case MIXER_CTL_TYPE_BYTE:
                ALOGI(" 0x%02x", mixer_ctl_get_value(ctl, i));
                break;
            default:
                ALOGI(" unknown");
                break;
        }
    }

    if (type == MIXER_CTL_TYPE_INT) {
        min = mixer_ctl_get_range_min(ctl);
        max = mixer_ctl_get_range_max(ctl);
        ALOGI(" (range %d->%d)", min, max);
    }
}

static void usb_soundcard_list_controls(struct mixer *mixer)
{
    struct mixer_ctl *ctl;
    const char *name, *type;
    unsigned int num_ctls, num_values;
    unsigned int i;

    num_ctls = mixer_get_num_ctls(mixer);

    ALOGI("Number of controls: %d\n", num_ctls);

    ALOGI("ctl\ttype\tnum\t%-40s value\n", "name");
    for (i = 0; i < num_ctls; i++) {
        ctl = mixer_get_ctl(mixer, i);
        if (ctl != NULL) {
            name = mixer_ctl_get_name(ctl);
            type = mixer_ctl_get_type_string(ctl);
            num_values = mixer_ctl_get_num_values(ctl);
            ALOGI("%d\t%s\t%d\t%-40s", i, type, num_values, name);
            if (name != NULL)
                usb_soundcard_detail_control(mixer, name);
        }
    }
}

static int usb_set_dev_id_mixer_ctl(unsigned int usb_usecase_type, int card,
                                    char *dev_mixer_ctl_name)
{
    struct mixer_ctl *ctl;
    unsigned int dev_token;
    unsigned int pcm_device_number = 0;

    /*
     * usb_dev_token_id is 32 bit number and is defined as below:
     * usb_sound_card_idx(31:16) | usb PCM device ID(15:8) | usb_usecase_type(7:0)
     */
    dev_token = (card << 16 ) |
                (pcm_device_number << 8) | (usb_usecase_type & 0xFF);

    ctl = mixer_get_ctl_by_name(usbmod->adev->mixer, dev_mixer_ctl_name);
    if (!ctl) {
       ALOGE("%s: Could not get ctl for mixer cmd - %s",
             __func__, dev_mixer_ctl_name);
       return -EINVAL;
    }
    mixer_ctl_set_value(ctl, 0, dev_token);

    return 0;
}

static int usb_set_endian_mixer_ctl(int endian, char *endian_mixer_ctl_name)
{
    struct mixer_ctl *ctl = mixer_get_ctl_by_name(usbmod->adev->mixer,
                                                  endian_mixer_ctl_name);
    if (!ctl) {
       ALOGE("%s: Could not get ctl for mixer cmd - %s",
             __func__, endian_mixer_ctl_name);
       return -EINVAL;
    }

    switch (endian) {
       case 0:
       case 1:
           mixer_ctl_set_value(ctl, 0, endian);
           break;
       default:
           ALOGW("%s: endianness(%d) not supported",
                 __func__, endian);
           break;
    }
    return 0;
}

static int usb_get_sample_rates(int type, char *rates_str,
                                struct usb_device_config *config)
{
    uint32_t i;
    char *next_sr_string, *temp_ptr;
    uint32_t sr, min_sr, max_sr, sr_size = 0;

    /* Sample rate string can be in any of the folloing two bit_widthes:
     * Rates: 8000 - 48000 (continuous)
     * Rates: 8000, 44100, 48000
     * Support both the bit_widths
     */
    ALOGV("%s: rates_str %s", __func__, rates_str);
    next_sr_string = strtok_r(rates_str, "Rates: ", &temp_ptr);
    if (next_sr_string == NULL) {
        ALOGE("%s: could not find min rates string", __func__);
        return -EINVAL;
    }
    if (strstr(rates_str, "continuous") != NULL) {
        min_sr = (uint32_t)atoi(next_sr_string);
        next_sr_string = strtok_r(NULL, " ,.-", &temp_ptr);
        if (next_sr_string == NULL) {
            ALOGE("%s: could not find max rates string", __func__);
            return -EINVAL;
        }
        max_sr = (uint32_t)atoi(next_sr_string);

        for (i = 0; i < MAX_SAMPLE_RATE_SIZE; i++) {
            if (supported_sample_rates[i] >= min_sr &&
                supported_sample_rates[i] <= max_sr) {
                // FIXME: we don't support >192KHz in recording path for now
                if ((supported_sample_rates[i] > SAMPLE_RATE_192000) &&
                        (type == USB_CAPTURE))
                    continue;
                config->rates[sr_size++] = supported_sample_rates[i];
                supported_sample_rates_mask[type] |= (1<<i);
                ALOGI_IF(usb_audio_debug_enable,
                    "%s: continuous sample rate supported_sample_rates[%d] %d",
                    __func__, i, supported_sample_rates[i]);
            }
        }
    } else {
        do {
            sr = (uint32_t)atoi(next_sr_string);
            // FIXME: we don't support >192KHz in recording path for now
            if ((sr > SAMPLE_RATE_192000) && (type == USB_CAPTURE)) {
                next_sr_string = strtok_r(NULL, " ,.-", &temp_ptr);
                continue;
            }

            for (i = 0; i < MAX_SAMPLE_RATE_SIZE; i++) {
                if (supported_sample_rates[i] == sr) {
                    ALOGI_IF(usb_audio_debug_enable,
                        "%s: sr %d, supported_sample_rates[%d] %d -> matches!!",
                        __func__, sr, i, supported_sample_rates[i]);
                    config->rates[sr_size++] = supported_sample_rates[i];
                    supported_sample_rates_mask[type] |= (1<<i);
                }
            }
            next_sr_string = strtok_r(NULL, " ,.-", &temp_ptr);
        } while (next_sr_string != NULL);
    }
    config->rate_size = sr_size;
    return 0;
}

static int get_usb_service_interval(const char *interval_str_start,
                                    struct usb_device_config *usb_device_info)
{
    unsigned long interval = 0;
    char time_unit[8] = {0};
    int multiplier = 0;

    char *eol = strchr(interval_str_start, '\n');
    if (!eol) {
        ALOGE("%s: No EOL found", __func__);
        return -1;
    }
    char *tmp = (char *)calloc(1, eol-interval_str_start+1);
    if (!tmp) {
        ALOGE("%s: failed to allocate tmp", __func__);
        return -1;
    }
    memcpy(tmp, interval_str_start, eol-interval_str_start);
    tmp[eol-interval_str_start] = '\0';
    sscanf(tmp, "%lu %2s", &interval, &time_unit[0]);
    if (!strcmp(time_unit, "us")) {
        multiplier = 1;
    } else if (!strcmp(time_unit, "ms")) {
        multiplier = 1000;
    } else if (!strcmp(time_unit, "s")) {
        multiplier = 1000000;
    } else {
        ALOGE("%s: unknown time_unit %s, assume default", __func__, time_unit);
        interval = DEFAULT_SERVICE_INTERVAL_US;
        multiplier = 1;
    }
    interval *= multiplier;
    ALOGD("%s: set service_interval_us %lu", __func__, interval);
    usb_device_info->service_interval_us = interval;
    free(tmp);
    return 0;
}

static int usb_get_capability(int type,
                              struct usb_card_config *usb_card_info,
                              int card)
{
    int32_t size = 0;
    int32_t fd=-1;
    int32_t channels_no;
    char *str_start = NULL;
    char *str_end = NULL;
    char *channel_start = NULL;
    char *bit_width_start = NULL;
    char *rates_str_start = NULL;
    char *target = NULL;
    char *read_buf = NULL;
    char *rates_str = NULL;
    char *interval_str_start = NULL;
    char path[128];
    int ret = 0;
    char *bit_width_str = NULL;
    struct usb_device_config * usb_device_info;
    bool check = false;

    memset(path, 0, sizeof(path));
    ALOGV("%s: for %s", __func__, (type == USB_PLAYBACK) ?
          PLAYBACK_PROFILE_STR : CAPTURE_PROFILE_STR);

    ret = snprintf(path, sizeof(path), "/proc/asound/card%u/stream0",
             card);
    if(ret < 0) {
        ALOGE("%s: failed on snprintf (%d) to path %s\n",
          __func__, ret, path);
        goto done;
    }

    fd = open(path, O_RDONLY);
    if (fd <0) {
        ALOGE("%s: error failed to open config file %s error: %d\n",
              __func__, path, errno);
        ret = -EINVAL;
        goto done;
    }

    read_buf = (char *)calloc(1, USB_BUFF_SIZE + 1);

    if (!read_buf) {
        ALOGE("Failed to create read_buf");
        ret = -ENOMEM;
        goto done;
    }

    if(read(fd, read_buf, USB_BUFF_SIZE) < 0) {
        ALOGE("file read error\n");
        goto done;
    }
    str_start = strstr(read_buf, ((type == USB_PLAYBACK) ?
                       PLAYBACK_PROFILE_STR : CAPTURE_PROFILE_STR));
    if (str_start == NULL) {
        ALOGE("%s: error %s section not found in usb config file",
               __func__, ((type == USB_PLAYBACK) ?
               PLAYBACK_PROFILE_STR : CAPTURE_PROFILE_STR));
        ret = -EINVAL;
        goto done;
    }
    str_end = strstr(read_buf, ((type == USB_PLAYBACK) ?
                       CAPTURE_PROFILE_STR : PLAYBACK_PROFILE_STR));
    if (str_end > str_start)
        check = true;

    ALOGV("%s: usb_config = %s, check %d\n", __func__, str_start, check);

    while (str_start != NULL) {
        str_start = strstr(str_start, "Altset");
        if ((str_start == NULL) || (check  && (str_start >= str_end))) {
            ALOGV("%s: done parsing %s\n", __func__, str_start);
            break;
        }
        ALOGV("%s: remaining string %s\n", __func__, str_start);
        str_start += sizeof("Altset");
        usb_device_info = calloc(1, sizeof(struct usb_device_config));
        if (usb_device_info == NULL) {
            ALOGE("%s: error unable to allocate memory",
                  __func__);
            ret = -ENOMEM;
            break;
        }
        usb_device_info->type = type;
        /* Bit bit_width parsing */
        bit_width_start = strstr(str_start, "Format: ");
        if (bit_width_start == NULL) {
            ALOGI("%s: Could not find bit_width string", __func__);
            free(usb_device_info);
            continue;
        }
        target = strchr(bit_width_start, '\n');
        if (target == NULL) {
            ALOGI("%s:end of line not found", __func__);
            free(usb_device_info);
            continue;
        }
        size = target - bit_width_start;
        if ((bit_width_str = (char *)malloc(size + 1)) == NULL) {
            ALOGE("%s: unable to allocate memory to hold bit width strings",
                  __func__);
            ret = -EINVAL;
            free(usb_device_info);
            break;
        }
        memcpy(bit_width_str, bit_width_start, size);
        bit_width_str[size] = '\0';

        const char * formats[] = { "S32", "S24_3", "S24", "S16" };
        const int    bit_width[] = { 32,  24,      24,     16};
        for (size_t i = 0; i < ARRAY_SIZE(formats); i++) {
            const char * s = strstr(bit_width_str, formats[i]);
            if (s) {
                usb_device_info->bit_width = bit_width[i];
                usb_card_info->endian = strstr(s, "BE") ? 1 : 0;
                break;
            }
        }

        if (bit_width_str)
            free(bit_width_str);

        /* channels parsing */
        channel_start = strstr(str_start, CHANNEL_NUMBER_STR);
        if (channel_start == NULL) {
            ALOGI("%s: could not find Channels string", __func__);
            free(usb_device_info);
            continue;
        }
        channels_no = atoi(channel_start + strlen(CHANNEL_NUMBER_STR));
        usb_device_info->channels =  channels_no;

        /* Sample rates parsing */
        rates_str_start = strstr(str_start, "Rates: ");
        if (rates_str_start == NULL) {
            ALOGI("%s: cant find rates string", __func__);
            free(usb_device_info);
            continue;
        }
        target = strchr(rates_str_start, '\n');
        if (target == NULL) {
            ALOGI("%s: end of line not found", __func__);
            free(usb_device_info);
            continue;
        }
        size = target - rates_str_start;
        if ((rates_str = (char *)malloc(size + 1)) == NULL) {
            ALOGE("%s: unable to allocate memory to hold sample rate strings",
                  __func__);
            ret = -EINVAL;
            free(usb_device_info);
            break;
        }
        memcpy(rates_str, rates_str_start, size);
        rates_str[size] = '\0';
        ret = usb_get_sample_rates(type, rates_str, usb_device_info);
        if (rates_str)
            free(rates_str);
        if (ret < 0) {
            ALOGI("%s: error unable to get sample rate values",
                  __func__);
            free(usb_device_info);
            continue;
        }
        // Data packet interval is an optional field.
        // Assume 0ms interval if this cannot be read
        // LPASS USB and HLOS USB will figure out the default to use
        usb_device_info->service_interval_us = DEFAULT_SERVICE_INTERVAL_US;
        interval_str_start = strstr(str_start, DATA_PACKET_INTERVAL_STR);
        if (interval_str_start != NULL) {
            interval_str_start += strlen(DATA_PACKET_INTERVAL_STR);
            ret = get_usb_service_interval(interval_str_start, usb_device_info);
            if (ret < 0) {
                ALOGE("%s: error unable to get service interval, assume default",
                      __func__);
            }
        }
        /* Add to list if every field is valid */
        list_add_tail(&usb_card_info->usb_device_conf_list,
                      &usb_device_info->list);
    }

done:
    if (fd >= 0) close(fd);
    if (read_buf) free(read_buf);
    return ret;
}

static int usb_get_device_pb_config(struct usb_card_config *usb_card_info,
                                    int card)
{
    int ret;

    /* get capabilities */
    if ((ret = usb_get_capability(USB_PLAYBACK, usb_card_info, card))) {
        ALOGE("%s: could not get Playback capabilities from usb device",
               __func__);
        goto exit;
    }
    usb_set_dev_id_mixer_ctl(USB_PLAYBACK, card, "USB_AUDIO_RX dev_token");
    usb_set_endian_mixer_ctl(usb_card_info->endian, "USB_AUDIO_RX endian");
exit:

    return ret;
}

static int usb_get_usbid(struct usb_card_config *usb_card_info,
                              int card)
{
    int32_t fd=-1;
    char path[128];
    int ret = 0;
    char *saveptr = NULL;

    memset(usb_card_info->usbid, 0, sizeof(usb_card_info->usbid));

    ret = snprintf(path, sizeof(path), "/proc/asound/card%u/usbid",
             card);

    if (ret < 0) {
        ALOGE("%s: failed on snprintf (%d) to path %s\n",
          __func__, ret, path);
        goto done;
    }

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        ALOGE("%s: error failed to open file %s error: %d\n",
              __func__, path, errno);
        ret = -EINVAL;
        goto done;
    }

    if (read(fd, usb_card_info->usbid, USBID_SIZE - 1) < 0) {
        ALOGE("file read error\n");
        ret = -EINVAL;
        usb_card_info->usbid[0] = '\0';
        goto done;
    }

    strtok_r(usb_card_info->usbid, "\n", &saveptr);

done:
    if (fd >= 0)
        close(fd);

    return ret;
}

static int usb_get_device_cap_config(struct usb_card_config *usb_card_info,
                                      int card)
{
    int ret;

    /* get capabilities */
    if ((ret = usb_get_capability(USB_CAPTURE, usb_card_info, card))) {
        ALOGE("%s: could not get Playback capabilities from usb device",
               __func__);
        goto exit;
    }
    usb_set_dev_id_mixer_ctl(USB_CAPTURE, card, "USB_AUDIO_TX dev_token");
    usb_set_endian_mixer_ctl(usb_card_info->endian, "USB_AUDIO_TX endian");

exit:
    return ret;
}

static void usb_get_sidetone_mixer(struct usb_card_config *usb_card_info)
{
    struct mixer_ctl *ctl;
    unsigned int index;

    for (index = 0; index < USB_SIDETONE_MAX_INDEX; index++)
        usb_card_info->usb_sidetone_index[index] = -1;

    usb_card_info->usb_snd_mixer = mixer_open(usb_card_info->usb_card);
    for (index = 0;
         index < sizeof(usb_sidetone_enable_str)/sizeof(usb_sidetone_enable_str[0]);
         index++) {
        ctl = mixer_get_ctl_by_name(usb_card_info->usb_snd_mixer,
                                    usb_sidetone_enable_str[index]);
        if (ctl) {
            usb_card_info->usb_sidetone_index[USB_SIDETONE_ENABLE_INDEX] = index;
            /* Disable device sidetone by default */
            mixer_ctl_set_value(ctl, 0, false);
            ALOGV("%s: sidetone mixer Control found(%s) ... disabling by default",
                    __func__, usb_sidetone_enable_str[index]);
            break;
        }
    }

    usb_get_sidetone_volume(usb_card_info);

    if ((usb_card_info->usb_snd_mixer != NULL) && (usb_audio_debug_enable))
        usb_soundcard_list_controls(usb_card_info->usb_snd_mixer);

    return;
}

static inline bool usb_output_device(audio_devices_t device) {
    // ignore accessory for now
    if (device == AUDIO_DEVICE_OUT_USB_ACCESSORY)
        return false;
    return audio_is_usb_out_device(device);
}

static inline bool usb_input_device(audio_devices_t device) {
    // ignore accessory for now
    if (device == AUDIO_DEVICE_IN_USB_ACCESSORY)
        return false;
    return audio_is_usb_in_device(device);
}

static bool usb_valid_device(audio_devices_t device)
{
    return usb_output_device(device) ||
           usb_input_device(device);
}

static void usb_print_active_device(void){
    struct listnode *node_i, *node_j;
    struct usb_device_config *dev_info;
    struct usb_card_config *card_info;
    unsigned int i;

    ALOGI("%s", __func__);
    list_for_each(node_i, &usbmod->usb_card_conf_list) {
        card_info = node_to_item(node_i, struct usb_card_config, list);
        ALOGI("%s: card_dev_type (0x%x), card_no(%d), %s",
               __func__,  card_info->usb_device_type,
              card_info->usb_card, card_info->endian ? "BE" : "LE");
        list_for_each(node_j, &card_info->usb_device_conf_list) {
            dev_info = node_to_item(node_j, struct usb_device_config, list);
            ALOGI("%s: bit-width(%d) channel(%d)",
                  __func__, dev_info->bit_width, dev_info->channels);
            for (i =  0; i < dev_info->rate_size; i++)
                ALOGI("%s: rate %d", __func__, dev_info->rates[i]);
        }
    }
}

static bool usb_get_best_match_for_bit_width(
                            struct listnode *dev_list,
                            unsigned int stream_bit_width,
                            unsigned int *bit_width)
{
    struct listnode *node_i;
    struct usb_device_config *dev_info;
    unsigned int candidate = 0;

    list_for_each(node_i, dev_list) {
        dev_info = node_to_item(node_i, struct usb_device_config, list);
        ALOGI_IF(usb_audio_debug_enable,
                 "%s: USB bw(%d), stream bw(%d), candidate(%d)",
                 __func__, dev_info->bit_width,
                 stream_bit_width, candidate);
        if (dev_info->bit_width == stream_bit_width) {
            *bit_width = dev_info->bit_width;
            ALOGV("%s: Found match bit-width (%d)",
                  __func__, dev_info->bit_width);
            goto exit;
        } else if (candidate == 0) {
                candidate = dev_info->bit_width;
        }
        /*
        * If stream bit is 24, USB supports both 16 bit and 32 bit, then
        *  higher bit width 32 is picked up instead of 16-bit
        */
        else if (ABS_SUB(stream_bit_width, dev_info->bit_width) <
                 ABS_SUB(stream_bit_width, candidate)) {
            candidate = dev_info->bit_width;
        }
        else if ((ABS_SUB(stream_bit_width, dev_info->bit_width) ==
                  ABS_SUB(stream_bit_width, candidate)) &&
                 (dev_info->bit_width > candidate)) {
            candidate = dev_info->bit_width;
        }
    }
    ALOGV("%s: No match found, use the best candidate bw(%d)",
          __func__, candidate);
    *bit_width = candidate;
exit:
    return true;
}

static bool usb_get_best_match_for_channels(
                            struct listnode *dev_list,
                            unsigned int bit_width,
                            unsigned int stream_ch,
                            unsigned int *ch)
{
    struct listnode *node_i;
    struct usb_device_config *dev_info;
    unsigned int candidate = 0;

    list_for_each(node_i, dev_list) {
        dev_info = node_to_item(node_i, struct usb_device_config, list);
        ALOGI_IF(usb_audio_debug_enable,
                 "%s: USB ch(%d)bw(%d), stream ch(%d)bw(%d), candidate(%d)",
                 __func__, dev_info->channels, dev_info->bit_width,
                 stream_ch, bit_width, candidate);
        if (dev_info->bit_width != bit_width)
            continue;
        if (dev_info->channels== stream_ch) {
            *ch = dev_info->channels;
            ALOGV("%s: Found match channels (%d)",
                  __func__, dev_info->channels);
            goto exit;
        } else if (candidate == 0)
                candidate = dev_info->channels;
            /*
            * If stream channel is 4, USB supports both 3 and 5, then
            *  higher channel 5 is picked up instead of 3
            */
        else if (ABS_SUB(stream_ch, dev_info->channels) <
                 ABS_SUB(stream_ch, candidate)) {
            candidate = dev_info->channels;
        } else if ((ABS_SUB(stream_ch, dev_info->channels) ==
                    ABS_SUB(stream_ch, candidate)) &&
                   (dev_info->channels > candidate)) {
            candidate = dev_info->channels;
        }
    }
    ALOGV("%s: No match found, use the best candidate ch(%d)",
          __func__, candidate);
    *ch = candidate;
exit:
    return true;

}

static bool usb_sample_rate_multiple(
                                     unsigned int stream_sample_rate,
                                     unsigned int base)
{
    return (((stream_sample_rate / base) * base) == stream_sample_rate);
}

static bool usb_find_sample_rate_candidate(unsigned int base,
                                           unsigned stream_rate,
                                           unsigned int usb_rate,
                                           unsigned int cur_candidate,
                                           unsigned int *update_candidate)
{
    /* For sample rate, we should consider  fracational sample rate as high priority.
    * For example, if the stream is 88.2kHz and USB device support both 44.1kH and
    * 48kHz sample rate, we should pick 44.1kHz instead of 48kHz
    */
    if (!usb_sample_rate_multiple(cur_candidate, base) &&
       usb_sample_rate_multiple(usb_rate, base)) {
        *update_candidate = usb_rate;
    } else if (usb_sample_rate_multiple(cur_candidate, base) &&
               usb_sample_rate_multiple(usb_rate, base)) {
        if (ABS_SUB(stream_rate, usb_rate) <
            ABS_SUB(stream_rate, cur_candidate)) {
            *update_candidate = usb_rate;
        } else if ((ABS_SUB(stream_rate, usb_rate) ==
                    ABS_SUB(stream_rate, cur_candidate)) &&
                   (usb_rate > cur_candidate)) {
            *update_candidate = usb_rate;
        }
    } else if (!usb_sample_rate_multiple(cur_candidate, base) &&
               !usb_sample_rate_multiple(usb_rate, base)) {
        if (ABS_SUB(stream_rate, usb_rate) <
            ABS_SUB(stream_rate, cur_candidate)) {
            *update_candidate = usb_rate;
        } else if ((ABS_SUB(stream_rate, usb_rate) ==
                    ABS_SUB(stream_rate, cur_candidate)) &&
                   (usb_rate > cur_candidate)) {
            *update_candidate = usb_rate;
        }
    }
    return true;
}

static bool usb_get_best_match_for_sample_rate(
                            struct listnode *dev_list,
                            unsigned int bit_width,
                            unsigned int ch,
                            unsigned int stream_sample_rate,
                            unsigned int *sr,
                            unsigned int service_interval,
                            bool do_service_interval_check)
{
    struct listnode *node_i;
    struct usb_device_config *dev_info;
    unsigned int candidate = 48000;
    unsigned int base = SAMPLE_RATE_8000;
    bool multiple_8k = usb_sample_rate_multiple(stream_sample_rate, base);
    unsigned int i;

    ALOGV("%s: stm ch(%d)bw(%d)sr(%d), stream sample multiple of 8kHz(%d)",
        __func__, ch, bit_width, stream_sample_rate, multiple_8k);

    list_for_each(node_i, dev_list) {
        dev_info = node_to_item(node_i, struct usb_device_config, list);
        ALOGI_IF(usb_audio_debug_enable,
                 "%s: USB ch(%d)bw(%d), stm ch(%d)bw(%d)sr(%d), candidate(%d)",
                 __func__, dev_info->channels, dev_info->bit_width,
                 ch, bit_width, stream_sample_rate, candidate);

        if ((dev_info->bit_width != bit_width) ||
            (dev_info->channels != ch) ||
            (do_service_interval_check && (dev_info->service_interval_us !=
                                           service_interval)))
            continue;

        candidate = 0;
        for (i = 0; i < dev_info->rate_size; i++) {
            ALOGI_IF(usb_audio_debug_enable,
                     "%s: USB ch(%d)bw(%d)sr(%d), stm ch(%d)bw(%d)sr(%d), candidate(%d)",
                     __func__, dev_info->channels,
                     dev_info->bit_width, dev_info->rates[i],
                     ch, bit_width, stream_sample_rate, candidate);
            if (stream_sample_rate == dev_info->rates[i]) {
                *sr = dev_info->rates[i];
                ALOGV("%s: Found match sample rate (%d)",
                      __func__, dev_info->rates[i]);
                goto exit;
            } else if (candidate == 0) {
                    candidate = dev_info->rates[i];
                /*
                * For sample rate, we should consider  fracational sample rate as high priority.
                * For example, if the stream is 88.2kHz and USB device support both 44.1kH and
                * 48kHz sample rate, we should pick 44.1kHz instead of 48kHz
                */
            } else if (multiple_8k) {
                usb_find_sample_rate_candidate(SAMPLE_RATE_8000,
                                               stream_sample_rate,
                                               dev_info->rates[i],
                                               candidate,
                                               &candidate);
            } else {
                usb_find_sample_rate_candidate(SAMPLE_RATE_11025,
                                               stream_sample_rate,
                                               dev_info->rates[i],
                                               candidate,
                                               &candidate);
            }
        }
    }
    ALOGV("%s: No match found, use the best candidate sr(%d)",
          __func__, candidate);
    *sr = candidate;
exit:
    return true;
}

static bool usb_audio_backend_apply_policy(struct listnode *dev_list,
                                           unsigned int *bit_width,
                                           unsigned int *sample_rate,
                                           unsigned int *ch)
{
    bool is_usb_supported = true;

    ALOGV("%s: from stream: bit-width(%d) sample_rate(%d) channels (%d)",
           __func__, *bit_width, *sample_rate, *ch);
    if (list_empty(dev_list)) {
        *sample_rate = 48000;
        *bit_width = 16;
        *ch = 2;
        ALOGI("%s: list is empty,fall back to default setting", __func__);
        goto exit;
    }
    usb_get_best_match_for_bit_width(dev_list, *bit_width, bit_width);
    usb_get_best_match_for_channels(dev_list,
                                    *bit_width,
                                    *ch,
                                    ch);
    usb_get_best_match_for_sample_rate(dev_list,
                                       *bit_width,
                                       *ch,
                                       *sample_rate,
                                       sample_rate,
                                       0 /*service int*/,
                                       false /*do service int check*/);
exit:
    ALOGV("%s: Updated sample rate per profile: bit-width(%d) rate(%d) chs(%d)",
           __func__, *bit_width, *sample_rate, *ch);
    return is_usb_supported;
}

void usb_set_sidetone_gain(struct str_parms *parms,
                                char *value, int len)
{
    int err;

    err = str_parms_get_str(parms, USB_SIDETONE_GAIN_STR,
                            value, len);
    if (err >= 0) {
        usb_sidetone_gain = pow(10.0, (float)(atoi(value))/10.0);
        ALOGV("%s: sidetone gain(%s) decimal %d",
              __func__, value, usb_sidetone_gain);
        str_parms_del(parms, USB_SIDETONE_GAIN_STR);
    }
    return;
}

int usb_enable_sidetone(int device, bool enable)
{
    int ret = -ENODEV;
    struct listnode *node_i;
    struct usb_card_config *card_info;
    int i;
    ALOGV("%s: card_dev_type (0x%x), sidetone enable(%d)",
           __func__,  device, enable);

    list_for_each(node_i, &usbmod->usb_card_conf_list) {
        card_info = node_to_item(node_i, struct usb_card_config, list);
        ALOGV("%s: card_dev_type (0x%x), card_no(%d)",
               __func__,  card_info->usb_device_type, card_info->usb_card);
        if (usb_output_device(card_info->usb_device_type)) {
            if ((i = card_info->usb_sidetone_index[USB_SIDETONE_ENABLE_INDEX]) != -1) {
                struct mixer_ctl *ctl = mixer_get_ctl_by_name(
                                card_info->usb_snd_mixer,
                                usb_sidetone_enable_str[i]);
                if (ctl)
                    mixer_ctl_set_value(ctl, 0, enable);
                else
                    break;

                if ((i = card_info->usb_sidetone_index[USB_SIDETONE_VOLUME_INDEX]) != -1) {
                    usb_set_sidetone_volume(card_info, enable, i);
                }

                ret = 0;
                break;
            }
        }
    }
    return ret;
}

bool usb_is_config_supported(unsigned int *bit_width,
                                        unsigned int *sample_rate,
                                        unsigned int *ch,
                                        bool is_playback)
{
    struct listnode *node_i;
    struct usb_card_config *card_info;
    bool is_usb_supported = false;

    ALOGV("%s: from stream: bit-width(%d) sample_rate(%d) ch(%d) is_playback(%d)",
           __func__, *bit_width, *sample_rate, *ch, is_playback);
    list_for_each(node_i, &usbmod->usb_card_conf_list) {
        card_info = node_to_item(node_i, struct usb_card_config, list);
        ALOGI_IF(usb_audio_debug_enable,
                 "%s: card_dev_type (0x%x), card_no(%d)",
                 __func__,  card_info->usb_device_type, card_info->usb_card);
        /* Currently only apply the first playback sound card configuration */
        if ((is_playback && usb_output_device(card_info->usb_device_type)) ||
            (!is_playback && usb_input_device(card_info->usb_device_type))){
            is_usb_supported = usb_audio_backend_apply_policy(
                                           &card_info->usb_device_conf_list,
                                           bit_width,
                                           sample_rate,
                                           ch);
            break;
        }
    }
    ALOGV("%s: updated: bit-width(%d) sample_rate(%d) channels (%d)",
           __func__, *bit_width, *sample_rate, *ch);

    return is_usb_supported;
}

int usb_get_max_channels(bool is_playback)
{
    struct listnode *node_i, *node_j;
    struct usb_device_config *dev_info;
    struct usb_card_config *card_info;
    unsigned int max_ch = 1;
    list_for_each(node_i, &usbmod->usb_card_conf_list) {
            card_info = node_to_item(node_i, struct usb_card_config, list);
            if (usb_output_device(card_info->usb_device_type) && !is_playback)
                continue;
            else if (usb_input_device(card_info->usb_device_type) && is_playback)
                continue;

            list_for_each(node_j, &card_info->usb_device_conf_list) {
                dev_info = node_to_item(node_j, struct usb_device_config, list);
                max_ch = _MAX(max_ch, dev_info->channels);
            }
    }

    return max_ch;
}

int usb_get_max_bit_width(bool is_playback)
{
    struct listnode *node_i, *node_j;
    struct usb_device_config *dev_info;
    struct usb_card_config *card_info;
    unsigned int max_bw = 16;
    list_for_each(node_i, &usbmod->usb_card_conf_list) {
            card_info = node_to_item(node_i, struct usb_card_config, list);
            if (usb_output_device(card_info->usb_device_type) && !is_playback)
                continue;
            else if (usb_input_device(card_info->usb_device_type) && is_playback)
                continue;

            list_for_each(node_j, &card_info->usb_device_conf_list) {
                dev_info = node_to_item(node_j, struct usb_device_config, list);
                max_bw = _MAX(max_bw, dev_info->bit_width);
            }
    }

    return max_bw;
}

int usb_get_sup_sample_rates(bool is_playback,
                                        uint32_t *sample_rates,
                                        uint32_t sample_rate_size)
{
    int type = is_playback ? USB_PLAYBACK : USB_CAPTURE;

    ALOGV("%s supported_sample_rates_mask 0x%x", __func__, supported_sample_rates_mask[type]);
    uint32_t bm = supported_sample_rates_mask[type];
    uint32_t tries = _MIN(sample_rate_size, (uint32_t)__builtin_popcount(bm));

    int i = 0;
    while (tries) {
        int idx = __builtin_ffs(bm) - 1;
        sample_rates[i++] = supported_sample_rates[idx];
        bm &= ~(1<<idx);
        tries--;
    }

    return i;
}

bool usb_is_capture_supported()
{
    if (usbmod == NULL) {
        ALOGE("%s: USB device object is NULL", __func__);
        return false;
    }
    ALOGV("%s: capture_supported %d",__func__,usbmod->is_capture_supported);
    return usbmod->is_capture_supported;
}

bool usb_is_tunnel_supported()
{
    return true;
}

void usb_add_device(audio_devices_t device, int card)
{
    struct usb_card_config *usb_card_info;
    char check_debug_enable[PROPERTY_VALUE_MAX];
    struct listnode *node_i;

    if (property_get("vendor.audio.usb.enable.debug",
                      check_debug_enable, NULL) > 0) {
        if (atoi(check_debug_enable))
            usb_audio_debug_enable = true;
    }

    ALOGI_IF(usb_audio_debug_enable,
             "%s: parameters device(0x%x), card(%d)",
             __func__, device, card);
    if (usbmod == NULL) {
        ALOGE("%s: USB device object is NULL", __func__);
        goto exit;
    }

    if (!(usb_valid_device(device)) || (card < 0)) {
        ALOGE("%s:device(0x%x), card(%d)",
              __func__, device, card);
        goto exit;
    }

    list_for_each(node_i, &usbmod->usb_card_conf_list) {
        usb_card_info = node_to_item(node_i, struct usb_card_config, list);
        ALOGI_IF(usb_audio_debug_enable,
                 "%s: list has capability for card_dev_type (0x%x), card_no(%d)",
                 __func__,  usb_card_info->usb_device_type, usb_card_info->usb_card);
        /* If we have cached the capability */
        if ((usb_card_info->usb_device_type == device) && (usb_card_info->usb_card == card)) {
            ALOGV("%s: capability for device(0x%x), card(%d) is cached, no need to update",
                  __func__, device, card);
            goto exit;
        }
    }
    usb_card_info = calloc(1, sizeof(struct usb_card_config));
    if (usb_card_info == NULL) {
        ALOGE("%s: error unable to allocate memory",
              __func__);
        goto exit;
    }
    list_init(&usb_card_info->usb_device_conf_list);
    if (usb_output_device(device)) {
        if (usb_get_usbid(usb_card_info, card) < 0) {
            ALOGE("parse card %d usbid fail", card);
        }

        if (!usb_get_device_pb_config(usb_card_info, card)){
            usb_card_info->usb_card = card;
            usb_card_info->usb_device_type = device;
            usb_get_sidetone_mixer(usb_card_info);
            list_add_tail(&usbmod->usb_card_conf_list, &usb_card_info->list);
            goto exit;
        }
    } else if (usb_input_device(device)) {
        if (usb_get_usbid(usb_card_info, card) < 0) {
            ALOGE("parse card %d usbid fail", card);
        }

        if (!usb_get_device_cap_config(usb_card_info, card)) {
            usb_card_info->usb_card = card;
            usb_card_info->usb_device_type = device;
            usbmod->is_capture_supported = true;
            list_add_tail(&usbmod->usb_card_conf_list, &usb_card_info->list);
            goto exit;
        }
    }
    /* free memory in error case */
    if (usb_card_info != NULL)
        free(usb_card_info);
exit:
    if (usb_audio_debug_enable)
        usb_print_active_device();
    return;
}

void usb_remove_device(audio_devices_t device, int card)
{
    struct listnode *node_i, *temp_i;
    struct listnode *node_j, *temp_j;
    struct usb_device_config *dev_info;
    struct usb_card_config *card_info;
    unsigned int i;

    ALOGV("%s: device(0x%x), card(%d)",
           __func__, device, card);

    if (usbmod == NULL) {
        ALOGE("%s: USB device object is NULL", __func__);
        goto exit;
    }

    if (!(usb_valid_device(device)) || (card < 0)) {
        ALOGE("%s: Invalid parameters device(0x%x), card(%d)",
              __func__, device, card);
        goto exit;
    }
    list_for_each_safe(node_i, temp_i, &usbmod->usb_card_conf_list) {
        card_info = node_to_item(node_i, struct usb_card_config, list);
        ALOGV("%s: card_dev_type (0x%x), card_no(%d)",
               __func__,  card_info->usb_device_type, card_info->usb_card);
        if ((device == card_info->usb_device_type) && (card == card_info->usb_card)){
            list_for_each_safe(node_j, temp_j, &card_info->usb_device_conf_list) {
                dev_info = node_to_item(node_j, struct usb_device_config, list);
                ALOGV("%s: bit-width(%d) channel(%d)",
                       __func__, dev_info->bit_width, dev_info->channels);
                for (i =  0; i < dev_info->rate_size; i++)
                    ALOGV("%s: rate %d", __func__, dev_info->rates[i]);

                list_remove(node_j);
                free(node_to_item(node_j, struct usb_device_config, list));
            }
            list_remove(node_i);
            free(node_to_item(node_i, struct usb_card_config, list));
        }
    }
    if (audio_is_usb_in_device(device)) { // XXX not sure if we need to check for card
        usbmod->is_capture_supported = false;
        supported_sample_rates_mask[USB_CAPTURE] = 0;
    } else
        supported_sample_rates_mask[USB_PLAYBACK] = 0;

exit:
    if (usb_audio_debug_enable)
        usb_print_active_device();

    return;
}

bool usb_alive(int card) {
    char path[PATH_MAX] = {0};
    // snprintf should never fail
    (void) snprintf(path, sizeof(path), "/proc/asound/card%u/stream0", card);
    return access(path, F_OK) == 0;
}

unsigned long usb_find_service_interval(bool min,
                                                   bool playback) {
    struct usb_card_config *card_info = NULL;
    struct usb_device_config *dev_info = NULL;
    struct listnode *node_i = NULL;
    struct listnode *node_j = NULL;
    unsigned long interval_us = min ? ULONG_MAX : 0;
    list_for_each(node_i, &usbmod->usb_card_conf_list) {
        card_info = node_to_item(node_i, struct usb_card_config, list);
        list_for_each(node_j, &card_info->usb_device_conf_list) {
            dev_info = node_to_item(node_j, struct usb_device_config, list);
            bool match = (playback && (dev_info->type == USB_PLAYBACK)) ||
                    (!playback && (dev_info->type == USB_CAPTURE));
            if (match) {
                interval_us = min ?
                        _MIN(interval_us, dev_info->service_interval_us) :
                        _MAX(interval_us, dev_info->service_interval_us);
            }
        }
        break;
    }
    return interval_us;
}

int usb_altset_for_service_interval(bool playback,
                                               unsigned long service_interval,
                                               uint32_t *bit_width,
                                               uint32_t *sample_rate,
                                               uint32_t *channels)
{
    struct usb_card_config *card_info = NULL;
    struct usb_device_config *dev_info = NULL;
    struct listnode *node_i = NULL;
    struct listnode *node_j = NULL;
    uint32_t bw = 0;
    uint32_t ch = 0;
    uint32_t sr = 0;

    if (service_interval == 0)
        return 0;
        /* not a valid service interval to search for */

#define FIND_BEST_MATCH(local_var, field, cond)                                      \
    list_for_each(node_i, &usbmod->usb_card_conf_list) {                             \
            /* Currently only apply the first playback sound card configuration */   \
            card_info = node_to_item(node_i, struct usb_card_config, list);          \
            list_for_each(node_j, &card_info->usb_device_conf_list) {                \
                    dev_info = node_to_item(node_j, struct usb_device_config, list); \
                    bool match = (playback && (dev_info->type == USB_PLAYBACK)) ||   \
                            (!playback && (dev_info->type == USB_CAPTURE));          \
                    if (match && (cond)) {                                           \
                        if (dev_info->field == *field) {                             \
                          local_var = dev_info->field;                               \
                          break;                                                     \
                        }                                                            \
                        local_var = _MAX(local_var, dev_info->field);                \
                    }                                                                \
            }                                                                        \
            break;                                                                   \
    }

    FIND_BEST_MATCH(bw, bit_width, dev_info->service_interval_us == service_interval);
    FIND_BEST_MATCH(ch, channels, \
                    dev_info->service_interval_us == service_interval && \
                    dev_info->bit_width == bw);
    sr = *sample_rate;
    list_for_each(node_i, &usbmod->usb_card_conf_list) {
        /* Currently only apply the first playback sound card configuration */
        card_info = node_to_item(node_i, struct usb_card_config, list);
        if ((playback && usb_output_device(card_info->usb_device_type)) ||
            (!playback && usb_input_device(card_info->usb_device_type))) {
            usb_get_best_match_for_sample_rate(&card_info->usb_device_conf_list,
                                               bw, ch, sr, &sr,
                                               service_interval,
                                               true);
        }
        break;
    }

#define SET_OR_RETURN_ON_ERROR(arg, local_var, cond) \
    if (local_var != (cond)) arg = local_var; else return -1;

    SET_OR_RETURN_ON_ERROR(*bit_width, bw, 0);
    SET_OR_RETURN_ON_ERROR(*sample_rate, sr, 0);
    SET_OR_RETURN_ON_ERROR(*channels, ch, 0);
    return 0;
#undef FIND_BEST_MATCH
#undef SET_OR_RETURN_ON_ERROR
}

int usb_get_service_interval(bool playback,
                                        unsigned long *service_interval)
{
    const char *ctl_name = "USB_AUDIO_RX service_interval";
    struct mixer_ctl *ctl = mixer_get_ctl_by_name(usbmod->adev->mixer,
                                                  ctl_name);

    if (!playback) {
        ALOGE("%s not valid for capture", __func__);
        return -1;
    }

    if (!ctl) {
        ALOGV("%s: could not get mixer %s", __func__, ctl_name);
        return -1;
    }

    *service_interval = mixer_ctl_get_value(ctl, 0);
    return 0;
}

int usb_set_service_interval(bool playback,
                                        unsigned long service_interval,
                                        bool *reconfig)
{
    *reconfig = false;
    unsigned long current_service_interval = 0;
    const char *ctl_name = "USB_AUDIO_RX service_interval";
    struct mixer_ctl *ctl = mixer_get_ctl_by_name(usbmod->adev->mixer,
                                                  ctl_name);

    if (!playback) {
        ALOGE("%s not valid for capture", __func__);
        return -1;
    }

    if (!ctl) {
        ALOGV("%s: could not get mixer %s", __func__, ctl_name);
        return -1;
    }

    if (usb_get_service_interval(playback,
                                            &current_service_interval) != 0) {
        ALOGE("%s Unable to get current service interval", __func__);
        return -1;
    }

    if (current_service_interval != service_interval) {
        mixer_ctl_set_value(ctl, 0, service_interval);
        *reconfig = usbmod->usb_reconfig = true;
    }
    else
        *reconfig = usbmod->usb_reconfig = false;
    return 0;
}

int usb_check_and_set_svc_int(struct audio_usecase *uc_info,
                                         bool starting_output_stream)
{
    struct listnode *node = NULL;
    struct audio_usecase *usecase = uc_info;
    bool reconfig = false;
    bool burst_mode = true;
    unsigned long service_interval = 0;
    struct audio_device *adev = usbmod->adev;

    ALOGV("%s: enter:", __func__);

    if ((starting_output_stream == true &&
        ((uc_info->id == USECASE_AUDIO_PLAYBACK_MMAP) ||
        (uc_info->id == USECASE_AUDIO_PLAYBACK_ULL))) ||
        (voice_is_call_state_active(usbmod->adev))) {
        burst_mode = false;
    } else {
    /* set if the valid usecase do not already exist */
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if (usecase->type == PCM_PLAYBACK &&
                is_usb_out_device_type(&usecase->device_list)) {
                switch (usecase->id) {
                    case USECASE_AUDIO_PLAYBACK_MMAP:
                    case USECASE_AUDIO_PLAYBACK_ULL:
                    {
                        if (uc_info != usecase) {
                            //another ULL stream exists
                            ALOGV("%s: another ULL Stream in active use-case list burst mode = false.", __func__);
                            burst_mode = false;
                        } else {
                            ALOGV("%s:current ULL uc is the same as incoming uc_info \
                                   which means we are stopping the output stream, \
                                   we don't want to set burst mode to false", __func__);
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
        }
    }

    ALOGV("%s: burst mode(%d).", __func__,burst_mode);

    service_interval =
            usb_find_service_interval(!burst_mode, true /*playback*/);

    usb_set_service_interval(true /*playback*/,
                                        service_interval,
                                        &reconfig);

    /* no change or not supported or no active usecases */
    if (reconfig)
        return -1;
    return 0;
}

bool usb_is_reconfig_req()
{
    return usbmod->usb_reconfig;
}

void usb_set_reconfig(bool is_required)
{
    usbmod->usb_reconfig = is_required;
}

bool usb_connected(struct str_parms *parms) {
    int card = -1;
    struct listnode *node_i = NULL;
    struct usb_card_config *usb_card_info = NULL;
    bool usb_connected = false;

    if ((parms != NULL) && str_parms_get_int(parms, "card", &card) >= 0) {
        usb_connected = usb_alive(card);
    } else {
        list_for_each(node_i, &usbmod->usb_card_conf_list) {
            usb_card_info = node_to_item(node_i, struct usb_card_config, list);
            if (usb_alive(usb_card_info->usb_card)) {
                usb_connected = true;
                break;
            }
        }
    }
    return usb_connected;
}

char *usb_usbid()
{
    struct usb_card_config *card_info;

    if (usbmod == NULL)
        return NULL;

    if (list_empty(&usbmod->usb_card_conf_list))
        return NULL;

    card_info = node_to_item(list_head(&usbmod->usb_card_conf_list),\
                             struct usb_card_config, list);

    return strdup(card_info->usbid);
}

void usb_init(void *adev)
{
    if (usbmod == NULL) {
        usbmod = calloc(1, sizeof(struct usb_module));
        if (usbmod == NULL) {
            ALOGE("%s: error unable to allocate memory", __func__);
            goto exit;
        }
    } else {
        memset(usbmod, 0, sizeof(*usbmod));
    }

    list_init(&usbmod->usb_card_conf_list);
    usbmod->adev = (struct audio_device*)adev;
    usbmod->sidetone_gain = usb_sidetone_gain;
    usbmod->is_capture_supported = false;
    usbmod->usb_reconfig = false;
exit:
    return;
}

void usb_deinit(void)
{
    if (NULL != usbmod){
        free(usbmod);
        usbmod = NULL;
    }
}
