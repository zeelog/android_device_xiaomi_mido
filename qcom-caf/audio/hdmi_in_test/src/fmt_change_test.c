/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright (C) 2015 The Android Open Source Project *
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

/* Test app to capture event updates from kernel */
/*#define LOG_NDEBUG 0*/
#include <getopt.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <pthread.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <utils/Log.h>
#include <signal.h>
#include <errno.h>
#include "qahw_api.h"
#include "qahw_defs.h"

/* add local define to prevent compilation errors on other platforms */
#ifndef AUDIO_DEVICE_IN_HDMI_ARC
#define AUDIO_DEVICE_IN_HDMI_ARC (AUDIO_DEVICE_BIT_IN | 0x8000000)
#endif

static int sock_event_fd = -1;

void *context = NULL;
FILE * log_file = NULL;
volatile bool stop_test = false;
volatile bool stop_record = false;
volatile bool record_active = false;

#define HDMI_SYS_PATH "/sys/devices/platform/soc/78b7000.i2c/i2c-3/3-0064/"
const char hdmi_in_audio_sys_path[] = HDMI_SYS_PATH "link_on0";
const char hdmi_in_power_on_sys_path[] = HDMI_SYS_PATH "power_on";
const char hdmi_in_audio_path_sys_path[] = HDMI_SYS_PATH "audio_path";
const char hdmi_in_arc_enable_sys_path[] = HDMI_SYS_PATH "arc_enable";

const char hdmi_in_audio_state_sys_path[] = HDMI_SYS_PATH "audio_state";
const char hdmi_in_audio_format_sys_path[] = HDMI_SYS_PATH "audio_format";
const char hdmi_in_audio_sample_rate_sys_path[] = HDMI_SYS_PATH "audio_rate";
const char hdmi_in_audio_layout_sys_path[] = HDMI_SYS_PATH "audio_layout";

#define SPDIF_SYS_PATH "/sys/devices/platform/soc/soc:qcom,msm-dai-q6-spdif-pri-tx/"
const char spdif_in_audio_state_sys_path[] = SPDIF_SYS_PATH "audio_state";
const char spdif_in_audio_format_sys_path[] = SPDIF_SYS_PATH "audio_format";
const char spdif_in_audio_sample_rate_sys_path[] = SPDIF_SYS_PATH "audio_rate";

#define SPDIF_ARC_SYS_PATH "/sys/devices/platform/soc/soc:qcom,msm-dai-q6-spdif-sec-tx/"
const char spdif_arc_in_audio_state_sys_path[] = SPDIF_ARC_SYS_PATH "audio_state";
const char spdif_arc_in_audio_format_sys_path[] = SPDIF_ARC_SYS_PATH "audio_format";
const char spdif_arc_in_audio_sample_rate_sys_path[] = SPDIF_ARC_SYS_PATH "audio_rate";

#define ID_RIFF 0x46464952
#define ID_WAVE 0x45564157
#define ID_FMT  0x20746d66
#define ID_DATA 0x61746164

#define FORMAT_PCM 1

struct wav_header {
    uint32_t riff_id;
    uint32_t riff_sz;
    uint32_t riff_fmt;
    uint32_t fmt_id;
    uint32_t fmt_sz;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;       /* sample_rate * num_channels * bps / 8 */
    uint16_t block_align;     /* num_channels * bps / 8 */
    uint16_t bits_per_sample;
    uint32_t data_id;
    uint32_t data_sz;
};

struct test_data {
    qahw_module_handle_t *qahw_mod_handle;
    audio_io_handle_t handle;
    audio_devices_t input_device;
    double record_length;
    int rec_cnt;

    char *audio_fmt_chg_text;
    int audio_fmt_chg_len;
    pthread_t record_th;
    pthread_t poll_event_th;
    pthread_attr_t poll_event_attr;

    int bit_width;
    audio_input_flags_t flags;
    audio_config_t config;
    audio_source_t source;

    int spdif_audio_state;
    int spdif_audio_mode;
    int spdif_sample_rate;
    int spdif_num_channels;

    int hdmi_power_on;
    int hdmi_audio_path;
    int hdmi_arc_enable;

    int hdmi_audio_state;
    int hdmi_audio_mode;
    int hdmi_audio_layout;
    int hdmi_sample_rate;
    int hdmi_num_channels;

    int spdif_arc_audio_state;
    int spdif_arc_audio_mode;
    int spdif_arc_sample_rate;
    int spdif_arc_num_channels;

    audio_devices_t new_input_device;

    audio_devices_t act_input_device; /* HDMI might use I2S and SPDIF */

    int act_audio_state;    /* audio active */
    int act_audio_mode;     /* 0=LPCM, 1=Compr */
    int act_sample_rate;    /* transmission sample rate */
    int act_num_channels;   /* transmission channels */
};

struct test_data tdata;

void stop_signal_handler(int signal)
{
   stop_test = true;
}

void *start_input(void *thread_param) {
    int rc = 0, ret = 0, count = 0;
    ssize_t bytes_read = -1;
    char file_name[256] = "/data/rec";
    int data_sz = 0, name_len = strlen(file_name);
    qahw_in_buffer_t in_buf;

    qahw_module_handle_t *qahw_mod_handle = tdata.qahw_mod_handle;

    /* convert/check params before use */
    tdata.config.sample_rate = tdata.act_sample_rate;

    if (tdata.act_audio_mode) {
        tdata.config.format = AUDIO_FORMAT_IEC61937;
        tdata.flags = QAHW_INPUT_FLAG_COMPRESS | QAHW_INPUT_FLAG_PASSTHROUGH;
    } else {
        if (tdata.bit_width == 32)
            tdata.config.format = AUDIO_FORMAT_PCM_8_24_BIT;
        else if (tdata.bit_width == 24)
            tdata.config.format = AUDIO_FORMAT_PCM_24_BIT_PACKED;
        else
            tdata.config.format = AUDIO_FORMAT_PCM_16_BIT;
        tdata.flags = 0;
    }

    switch (tdata.act_num_channels) {
    case 2:
        tdata.config.channel_mask = AUDIO_CHANNEL_IN_STEREO;
        break;
    case 8:
        tdata.config.channel_mask = AUDIO_CHANNEL_INDEX_MASK_8;
        break;
    default:
        fprintf(log_file,
            "ERROR :::: channel count %d not supported\n",
            tdata.act_num_channels);
        pthread_exit(0);
    }
    tdata.config.frame_count = 0;

    /* Open audio input stream */
    qahw_stream_handle_t* in_handle = NULL;

    rc = qahw_open_input_stream(qahw_mod_handle, tdata.handle,
        tdata.act_input_device, &tdata.config, &in_handle, tdata.flags,
        "input_stream", tdata.source);
    if (rc) {
        fprintf(log_file,
            "ERROR :::: Could not open input stream, handle(%d)\n",
            tdata.handle);
        pthread_exit(0);
    }

    /* Get buffer size to get upper bound on data to read from the HAL */
    size_t buffer_size = qahw_in_get_buffer_size(in_handle);
    char *buffer = (char *) calloc(1, buffer_size);
    size_t written_size;
    if (buffer == NULL) {
        fprintf(log_file, "calloc failed!!, handle(%d)\n", tdata.handle);
        pthread_exit(0);
    }

    fprintf(log_file, " input opened, buffer  %p, size %zu, handle(%d)\n", buffer,
        buffer_size, tdata.handle);

    /* set profile for the recording session */
    qahw_in_set_parameters(in_handle, "audio_stream_profile=record_unprocessed");

    if (audio_is_linear_pcm(tdata.config.format))
        snprintf(file_name + name_len, sizeof(file_name) - name_len, "%d.wav",
            tdata.rec_cnt);
    else
        snprintf(file_name + name_len, sizeof(file_name) - name_len, "%d.raw",
            tdata.rec_cnt);

    tdata.rec_cnt++;

    FILE *fd = fopen(file_name, "w");
    if (fd == NULL) {
        fprintf(log_file, "File open failed\n");
        free(buffer);
        pthread_exit(0);
    }

    int bps = 16;

    switch (tdata.config.format) {
    case AUDIO_FORMAT_PCM_24_BIT_PACKED:
        bps = 24;
        break;
    case AUDIO_FORMAT_PCM_8_24_BIT:
    case AUDIO_FORMAT_PCM_32_BIT:
        bps = 32;
        break;
    case AUDIO_FORMAT_PCM_16_BIT:
    default:
        bps = 16;
    }

    struct wav_header hdr;
    hdr.riff_id = ID_RIFF;
    hdr.riff_sz = 0;
    hdr.riff_fmt = ID_WAVE;
    hdr.fmt_id = ID_FMT;
    hdr.fmt_sz = 16;
    hdr.audio_format = FORMAT_PCM;
    hdr.num_channels = tdata.act_num_channels;
    hdr.sample_rate = tdata.config.sample_rate;
    hdr.byte_rate = hdr.sample_rate * hdr.num_channels * (bps / 8);
    hdr.block_align = hdr.num_channels * (bps / 8);
    hdr.bits_per_sample = bps;
    hdr.data_id = ID_DATA;
    hdr.data_sz = 0;
    if (audio_is_linear_pcm(tdata.config.format))
        fwrite(&hdr, 1, sizeof(hdr), fd);

    memset(&in_buf, 0, sizeof(qahw_in_buffer_t));
    while (true && !stop_record) {
        in_buf.buffer = buffer;
        in_buf.bytes = buffer_size;
        bytes_read = qahw_in_read(in_handle, &in_buf);

        written_size = fwrite(in_buf.buffer, 1, bytes_read, fd);
        if (written_size < bytes_read) {
            printf("Error in fwrite(%d)=%s\n", ferror(fd),
                strerror(ferror(fd)));
            break;
        }
        data_sz += bytes_read;
    }

    if (audio_is_linear_pcm(tdata.config.format)) {
        /* update lengths in header */
        hdr.data_sz = data_sz;
        hdr.riff_sz = data_sz + 44 - 8;
        fseek(fd, 0, SEEK_SET);
        fwrite(&hdr, 1, sizeof(hdr), fd);
    }
    free(buffer);
    fclose(fd);
    fd = NULL;

    fprintf(log_file, " closing input, handle(%d), written %d bytes", tdata.handle, data_sz);

    /* Close input stream and device. */
    rc = qahw_in_standby(in_handle);
    if (rc) {
        fprintf(log_file, "in standby failed %d, handle(%d)\n", rc,
            tdata.handle);
    }

    rc = qahw_close_input_stream(in_handle);
    if (rc) {
        fprintf(log_file, "could not close input stream %d, handle(%d)\n", rc,
            tdata.handle);
    }

    fprintf(log_file,
        "\n\n The audio recording has been saved to %s.\n"
        "The audio data has the  following characteristics:\n Sample rate: %i\n Format: %d\n "
        "Num channels: %i, handle(%d)\n\n", file_name,
        tdata.config.sample_rate, tdata.config.format, tdata.act_num_channels,
        tdata.handle);

    return NULL;
}

void start_rec_thread(void)
{
    int ret = 0;

    stop_record = false;
    record_active = true;

    fprintf(log_file, "\n Create record thread \n");
    ret = pthread_create(&tdata.record_th, NULL, start_input, (void *)&tdata);
    if (ret) {
        fprintf(log_file, " Failed to create record thread\n");
        exit(1);
   }
}

void stop_rec_thread(void)
{
    if (record_active) {
        record_active = false;
        stop_record = true;
        fprintf(log_file, "\n Stop record thread \n");
        pthread_join(tdata.record_th, NULL);
    }
}


void read_data_from_fd(const char* path, int *value)
{
    int fd = -1;
    char buf[16];
    int ret;

    fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        ALOGE("Unable open fd for file %s", path);
        return;
    }

    ret = read(fd, buf, 15);
    if (ret < 0) {
        ALOGE("File %s Data is empty", path);
        close(fd);
        return;
    }

    buf[ret] = '\0';
    *value = atoi(buf);
    close(fd);
}

void get_input_status()
{
    switch (tdata.input_device) {
    case AUDIO_DEVICE_IN_SPDIF:
        read_data_from_fd(spdif_in_audio_state_sys_path, &tdata.spdif_audio_state);
        read_data_from_fd(spdif_in_audio_format_sys_path, &tdata.spdif_audio_mode);
        read_data_from_fd(spdif_in_audio_sample_rate_sys_path, &tdata.spdif_sample_rate);
        tdata.spdif_num_channels = 2;
        tdata.new_input_device = AUDIO_DEVICE_IN_SPDIF;

        fprintf(log_file, "spdif audio_state: %d, audio_format: %d, sample_rate: %d, num_channels: %d\n",
            tdata.spdif_audio_state, tdata.spdif_audio_mode, tdata.spdif_sample_rate, tdata.spdif_num_channels);
        break;
    case AUDIO_DEVICE_IN_HDMI:
        read_data_from_fd(hdmi_in_power_on_sys_path, &tdata.hdmi_power_on);
        read_data_from_fd(hdmi_in_audio_path_sys_path, &tdata.hdmi_audio_path);
        read_data_from_fd(hdmi_in_arc_enable_sys_path, &tdata.hdmi_arc_enable);

        read_data_from_fd(hdmi_in_audio_state_sys_path, &tdata.hdmi_audio_state);
        read_data_from_fd(hdmi_in_audio_format_sys_path, &tdata.hdmi_audio_mode);
        read_data_from_fd(hdmi_in_audio_sample_rate_sys_path, &tdata.hdmi_sample_rate);
        read_data_from_fd(hdmi_in_audio_layout_sys_path, &tdata.hdmi_audio_layout);
        if (tdata.hdmi_audio_layout)
            tdata.hdmi_num_channels = 8;
        else
            tdata.hdmi_num_channels = 2;
        /* todo: read ch_count, ch_alloc */

        read_data_from_fd(spdif_arc_in_audio_state_sys_path, &tdata.spdif_arc_audio_state);
        read_data_from_fd(spdif_arc_in_audio_format_sys_path, &tdata.spdif_arc_audio_mode);
        read_data_from_fd(spdif_arc_in_audio_sample_rate_sys_path, &tdata.spdif_arc_sample_rate);
        tdata.spdif_arc_num_channels = 2;

        if (tdata.hdmi_arc_enable ||
            (tdata.hdmi_audio_state && (tdata.hdmi_audio_layout == 0) && tdata.hdmi_audio_mode)) {
            tdata.new_input_device = AUDIO_DEVICE_IN_HDMI_ARC;
            fprintf(log_file, "hdmi audio interface SPDIF_ARC\n");
        } else {
            tdata.new_input_device = AUDIO_DEVICE_IN_HDMI;
            fprintf(log_file, "hdmi audio interface MI2S\n");
        }

        fprintf(log_file, "hdmi audio_state: %d, audio_format: %d, sample_rate: %d, num_channels: %d\n",
            tdata.hdmi_audio_state, tdata.hdmi_audio_mode, tdata.hdmi_sample_rate, tdata.hdmi_num_channels);
        fprintf(log_file, "arc  audio_state: %d, audio_format: %d, sample_rate: %d, num_channels: %d\n",
            tdata.spdif_arc_audio_state, tdata.spdif_arc_audio_mode, tdata.spdif_arc_sample_rate, tdata.spdif_arc_num_channels);
        break;
    }
}

void input_restart_check(void)
{
    get_input_status();

    switch (tdata.input_device) {
    case AUDIO_DEVICE_IN_SPDIF:
        if ((tdata.act_input_device != tdata.new_input_device) ||
            (tdata.spdif_audio_state == 2)) {
            fprintf(log_file, "old       audio_state: %d, audio_format: %d, rate: %d, channels: %d\n",
                tdata.act_audio_state, tdata.act_audio_mode,
                tdata.act_sample_rate, tdata.act_num_channels);
            fprintf(log_file, "new spdif audio_state: %d, audio_format: %d, rate: %d, channels: %d\n",
                tdata.spdif_audio_state, tdata.spdif_audio_mode,
                tdata.spdif_sample_rate, tdata.spdif_num_channels);

            stop_rec_thread();

            tdata.act_input_device = AUDIO_DEVICE_IN_SPDIF;
            tdata.act_audio_state = 1;
            tdata.act_audio_mode = tdata.spdif_audio_mode;
            tdata.act_sample_rate = tdata.spdif_sample_rate;
            tdata.act_num_channels = tdata.spdif_num_channels;

            start_rec_thread();
        }
        break;
    case AUDIO_DEVICE_IN_HDMI:
        if (tdata.act_input_device != tdata.new_input_device) {
            stop_rec_thread();

            if (tdata.new_input_device == AUDIO_DEVICE_IN_HDMI) {
                fprintf(log_file, "old      audio_state: %d, audio_format: %d, rate: %d, channels: %d\n",
                    tdata.act_audio_state, tdata.act_audio_mode,
                    tdata.act_sample_rate, tdata.act_num_channels);
                fprintf(log_file, "new hdmi audio_state: %d, audio_format: %d, rate: %d, channels: %d\n",
                    tdata.hdmi_audio_state, tdata.hdmi_audio_mode,
                    tdata.hdmi_sample_rate, tdata.hdmi_num_channels);

                tdata.act_input_device = AUDIO_DEVICE_IN_HDMI;
                tdata.act_audio_state = tdata.hdmi_audio_state;
                tdata.act_audio_mode = tdata.hdmi_audio_mode;
                tdata.act_sample_rate = tdata.hdmi_sample_rate;
                tdata.act_num_channels = tdata.hdmi_num_channels;

                if (tdata.hdmi_audio_state)
                    start_rec_thread();
            } else {
                tdata.act_input_device = AUDIO_DEVICE_IN_HDMI_ARC;
                if (tdata.hdmi_arc_enable) {
                    fprintf(log_file, "old     audio_state: %d, audio_format: %d, rate: %d, channels: %d\n",
                        tdata.act_audio_state, tdata.act_audio_mode,
                        tdata.act_sample_rate, tdata.act_num_channels);
                    fprintf(log_file, "new arc audio_state: %d, audio_format: %d, rate: %d, channels: %d\n",
                        tdata.spdif_arc_audio_state, tdata.spdif_arc_audio_mode,
                        tdata.spdif_arc_sample_rate, tdata.spdif_arc_num_channels);

                    tdata.act_audio_state = 1;
                    tdata.act_audio_mode = tdata.spdif_arc_audio_mode;
                    tdata.act_sample_rate = tdata.spdif_arc_sample_rate;
                    tdata.act_num_channels = tdata.spdif_arc_num_channels;
                } else {
                    fprintf(log_file, "old      audio_state: %d, audio_format: %d, rate: %d, channels: %d\n",
                        tdata.act_audio_state, tdata.act_audio_mode,
                        tdata.act_sample_rate, tdata.act_num_channels);
                    fprintf(log_file, "new arc (from hdmi) audio_state: %d, audio_format: %d, rate: %d, channels: %d\n",
                        tdata.hdmi_audio_state, tdata.hdmi_audio_mode,
                        tdata.hdmi_sample_rate, tdata.hdmi_num_channels);

                    tdata.act_audio_state = 1;
                    tdata.act_audio_mode = tdata.hdmi_audio_mode;
                    tdata.act_sample_rate = tdata.hdmi_sample_rate;
                    tdata.act_num_channels = tdata.hdmi_num_channels;
                }
                start_rec_thread();
            }
        } else { /* check for change on same audio device */
            if (tdata.new_input_device == AUDIO_DEVICE_IN_HDMI) {
                if ((tdata.act_audio_state != tdata.hdmi_audio_state) ||
                    (tdata.act_audio_mode != tdata.hdmi_audio_mode) ||
                    (tdata.act_sample_rate != tdata.hdmi_sample_rate) ||
                    (tdata.act_num_channels != tdata.hdmi_num_channels)) {

                    fprintf(log_file, "old      audio_state: %d, audio_format: %d, rate: %d, channels: %d\n",
                        tdata.act_audio_state, tdata.act_audio_mode,
                        tdata.act_sample_rate, tdata.act_num_channels);
                    fprintf(log_file, "new hdmi audio_state: %d, audio_format: %d, rate: %d, channels: %d\n",
                        tdata.hdmi_audio_state, tdata.hdmi_audio_mode,
                        tdata.hdmi_sample_rate, tdata.hdmi_num_channels);

                    stop_rec_thread();

                    tdata.act_audio_state = tdata.hdmi_audio_state;
                    tdata.act_audio_mode = tdata.hdmi_audio_mode;
                    tdata.act_sample_rate = tdata.hdmi_sample_rate;
                    tdata.act_num_channels = tdata.hdmi_num_channels;

                    if (tdata.hdmi_audio_state)
                        start_rec_thread();
                    }
            } else {
                if (tdata.spdif_arc_audio_state == 2) {
                    fprintf(log_file, "old     audio_state: %d, audio_format: %d, rate: %d, channels: %d\n",
                        tdata.act_audio_state, tdata.act_audio_mode,
                        tdata.act_sample_rate, tdata.act_num_channels);
                    fprintf(log_file, "new arc audio_state: %d, audio_format: %d, rate: %d, channels: %d\n",
                        tdata.spdif_arc_audio_state, tdata.spdif_arc_audio_mode,
                        tdata.spdif_arc_sample_rate, tdata.spdif_arc_num_channels);

                    stop_rec_thread();

                    tdata.act_audio_state = 1;
                    tdata.act_audio_mode = tdata.spdif_arc_audio_mode;
                    tdata.act_sample_rate = tdata.spdif_arc_sample_rate;
                    tdata.act_num_channels = tdata.spdif_arc_num_channels;

                    start_rec_thread();
                }
            }
        }
        break;
    }
}

int poll_event_init()
{
    struct sockaddr_nl sock_addr;
    int sz = (64*1024);
    int soc;

    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.nl_family = AF_NETLINK;
    sock_addr.nl_pid = getpid();
    sock_addr.nl_groups = 0xffffffff;

    soc = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
    if (soc < 0) {
        return 0;
    }

    setsockopt(soc, SOL_SOCKET, SO_RCVBUFFORCE, &sz, sizeof(sz));

    if (bind(soc, (struct sockaddr*) &sock_addr, sizeof(sock_addr)) < 0) {
        close(soc);
        return 0;
    }

    sock_event_fd = soc;

    return (soc > 0);
}

void* listen_uevent()
{
    char buffer[64*1024];
    struct pollfd fds;
    int i, count;
    int j;
    char *dev_path = NULL;
    char *switch_state = NULL;
    char *switch_name = NULL;
    int audio_changed;

    input_restart_check();

    while(!stop_test) {

        fds.fd = sock_event_fd;
        fds.events = POLLIN;
        fds.revents = 0;
        i = poll(&fds, 1, 5); /* wait 5 msec */

        if (i > 0 && (fds.revents & POLLIN)) {
            count = recv(sock_event_fd, buffer, (64*1024), 0 );
            if (count > 0) {
                buffer[count] = '\0';
                audio_changed = 0;
                j = 0;
                while(j < count) {
                    if (strncmp(&buffer[j], "DEVPATH=", 8) == 0) {
                        dev_path = &buffer[j+8];
                        j += 8;
                        continue;
                    } else if (tdata.input_device == AUDIO_DEVICE_IN_SPDIF) {
                        if (strncmp(&buffer[j], "PRI_SPDIF_TX=MEDIA_CONFIG_CHANGE", strlen("PRI_SPDIF_TX=MEDIA_CONFIG_CHANGE")) == 0) {
                            audio_changed = 1;
                            ALOGI("AUDIO CHANGE EVENT: %s\n", &buffer[j]);
                            j += strlen("PRI_SPDIF_TX=MEDIA_CONFIG_CHANGE");
                            continue;
                        }
                    } else if (tdata.input_device == AUDIO_DEVICE_IN_HDMI) {
                        if (strncmp(&buffer[j], "EP92EVT_AUDIO=MEDIA_CONFIG_CHANGE", strlen("EP92EVT_AUDIO=MEDIA_CONFIG_CHANGE")) == 0) {
                            audio_changed = 1;
                            ALOGI("AUDIO CHANGE EVENT: %s\n", &buffer[j]);
                            j += strlen("EP92EVT_AUDIO=MEDIA_CONFIG_CHANGE");
                            continue;
                        } else if (strncmp(&buffer[j], "SEC_SPDIF_TX=MEDIA_CONFIG_CHANGE", strlen("SEC_SPDIF_TX=MEDIA_CONFIG_CHANGE")) == 0) {
                            audio_changed = 1;
                            ALOGI("AUDIO CHANGE EVENT: %s\n", &buffer[j]);
                            j += strlen("SEC_SPDIF_TX=MEDIA_CONFIG_CHANGE");
                            continue;
                        } else if (strncmp(&buffer[j], "EP92EVT_", 8) == 0) {
                            ALOGI("EVENT: %s\n", &buffer[j]);
                            j += 8;
                            continue;
                        }
                    }
                    j++;
                }

                if (audio_changed)
                    input_restart_check();
            }
        } else {
            ALOGV("NO Data\n");
        }
    }

    stop_rec_thread();
}

void fill_default_params(struct test_data *tdata) {
    memset(tdata, 0, sizeof(struct test_data));

    tdata->input_device = AUDIO_DEVICE_IN_SPDIF;
    tdata->bit_width = 24;
    tdata->source = AUDIO_SOURCE_UNPROCESSED;
    tdata->record_length = 8 /*sec*/;

    tdata->handle = 0x99A;
}

void usage() {
    printf(" \n Command \n");
    printf(" \n fmt_change_test <options>\n");
    printf(" \n Options\n");
    printf(" -d  --device <int>                 - see system/media/audio/include/system/audio.h for device values\n");
    printf("                                      spdif_in 2147549184, hdmi_in 2147483680\n");
    printf("                                      Optional Argument and Default value is spdif_in\n\n");
    printf(" -b  --bits  <int>                  - Bitwidth in PCM mode (16, 24 or 32), Default is 24\n\n");
    printf(" -F  --flags  <int>                 - Integer value of flags to be used for opening input stream\n\n");
    printf(" -t  --recording-time <in seconds>  - Time duration for the recording\n\n");
    printf(" -l  --log-file <FILEPATH>          - File path for debug msg, to print\n");
    printf("                                      on console use stdout or 1 \n\n");
    printf(" -h  --help                         - Show this help\n\n");
    printf(" \n Examples \n");
    printf(" hdmi_in_event_test                          -> start a recording stream with default configurations\n\n");
    printf(" hdmi_in_event_test -d 2147483680 -t 20      -> start a recording session, with device hdmi_in,\n");
    printf("                                                record data for 20 secs.\n\n");
}

static void qti_audio_server_death_notify_cb(void *ctxt) {
    fprintf(log_file, "qas died\n");
    fprintf(stderr, "qas died\n");
    stop_test = true;
    stop_record = true;
}

int main(int argc, char* argv[])
{
    qahw_module_handle_t *qahw_mod_handle;
    const  char *mod_name = "audio.primary";

    char log_filename[256] = "stdout";
    int i;
    int ret = -1;

    log_file = stdout;
    fill_default_params(&tdata);
    struct option long_options[] = {
        /* These options set a flag. */
        {"device",          required_argument,    0, 'd'},
        {"bits",            required_argument,    0, 'b'},
        {"flags",           required_argument,    0, 'F'},
        {"recording-time",  required_argument,    0, 't'},
        {"log-file",        required_argument,    0, 'l'},
        {"help",            no_argument,          0, 'h'},
        {0, 0, 0, 0}
    };

    int opt = 0;
    int option_index = 0;
    while ((opt = getopt_long(argc,
                              argv,
                              "-d:b:F:t:l:h",
                              long_options,
                              &option_index)) != -1) {
            switch (opt) {
            case 'd':
                tdata.input_device = atoll(optarg);
                break;
            case 'b':
                tdata.bit_width = atoll(optarg);
                break;
            case 'F':
                tdata.flags = atoll(optarg);
                break;
            case 't':
                tdata.record_length = atoi(optarg);
                break;
            case 'l':
                snprintf(log_filename, sizeof(log_filename), "%s", optarg);
                break;
            case 'h':
                usage();
                return 0;
                break;
         }
    }
    fprintf(log_file, "registering qas callback");
    qahw_register_qas_death_notify_cb((audio_error_callback)qti_audio_server_death_notify_cb, context);

    switch (tdata.input_device) {
    case AUDIO_DEVICE_IN_SPDIF:
        break;
    case AUDIO_DEVICE_IN_HDMI:
        break;
    default:
        fprintf(log_file, "device %d not supported\n", tdata.input_device);
        return -1;
    }

    switch (tdata.bit_width) {
    case 16:
    case 24:
    case 32:
        break;
    default:
        fprintf(log_file, "bitwidth %d not supported\n", tdata.bit_width);
        return -1;
    }

    qahw_mod_handle = qahw_load_module(mod_name);
    if(qahw_mod_handle == NULL) {
        fprintf(log_file, " qahw_load_module failed");
        return -1;
    }
    fprintf(log_file, " Starting audio recording test. \n");
    if (strcasecmp(log_filename, "stdout") && strcasecmp(log_filename, "1")) {
        if ((log_file = fopen(log_filename,"wb"))== NULL) {
            fprintf(stderr, "Cannot open log file %s\n", log_filename);
            /* continue to log to std out */
            log_file = stdout;
        }
    }

    tdata.qahw_mod_handle = qahw_mod_handle;

    /* Register the SIGINT to close the App properly */
    if (signal(SIGINT, stop_signal_handler) == SIG_ERR)
        fprintf(log_file, "Failed to register SIGINT:%d\n", errno);

    /* Register the SIGTERM to close the App properly */
    if (signal(SIGTERM, stop_signal_handler) == SIG_ERR)
        fprintf(log_file, "Failed to register SIGTERM:%d\n", errno);

    time_t start_time = time(0);
    double time_elapsed = 0;

    pthread_attr_init(&tdata.poll_event_attr);
    pthread_attr_setdetachstate(&tdata.poll_event_attr, PTHREAD_CREATE_JOINABLE);
    poll_event_init();
    pthread_create(&tdata.poll_event_th, &tdata.poll_event_attr,
                       (void *) listen_uevent, NULL);

    while(true && !stop_test) {
        time_elapsed = difftime(time(0), start_time);
        if (tdata.record_length && (time_elapsed > tdata.record_length)) {
            fprintf(log_file, "\n Test completed.\n");
            stop_test = true;
            break;
        }
    }

    fprintf(log_file, "\n Stop test \n");

    pthread_join(tdata.poll_event_th, NULL);

    fprintf(log_file, "\n Unload HAL\n");

    ret = qahw_unload_module(qahw_mod_handle);
    if (ret) {
        fprintf(log_file, "could not unload hal %d\n", ret);
    }

    fprintf(log_file, "Done with hal record test\n");
    if (log_file != stdout) {
        if (log_file) {
          fclose(log_file);
          log_file = NULL;
        }
    }

    return 0;
}
