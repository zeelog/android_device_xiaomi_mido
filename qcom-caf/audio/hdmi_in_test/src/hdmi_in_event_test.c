/*
 * Copyright (c) 2017, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *    * Neither the name of The Linux Foundation nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
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

/* Test app to capture event updates from kernel */
/*#define LOG_NDEBUG 0*/
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
#include "edid.h"

static int sock_event_fd = -1;

int hdmi_conn_state = 0;
int hdmi_audio_state = 0;
int hdmi_audio_format = 0;
int hdmi_sample_rate = 0;
int hdmi_num_channels = 0;

const char hdmi_in_audio_sys_path[] =
                               "/sys/devices/virtual/switch/hpd_state/state";
const char hdmi_in_audio_dev_path[] = "/devices/virtual/switch/hpd_state";
const char hdmi_in_audio_state_sys_path[] =
                               "/sys/devices/virtual/switch/audio_state/state";
const char hdmi_in_audio_state_dev_path[] =
                               "/devices/virtual/switch/audio_state";
const char hdmi_in_audio_format_sys_path[] =
                               "/sys/devices/virtual/switch/audio_format/state";
const char hdmi_in_audio_format_dev_path[] =
                               "/devices/virtual/switch/audio_format";
const char hdmi_in_audio_sample_rate_sys_path[] =
                               "/sys/devices/virtual/switch/sample_rate/state";
const char hdmi_in_audio_sample_rate_dev_path[] =
                               "/devices/virtual/switch/sample_rate";
const char hdmi_in_audio_channel_sys_path[] =
                               "/sys/devices/virtual/switch/channels/state";
const char hdmi_in_audio_channel_dev_path[] =
                               "/devices/virtual/switch/channels";

pthread_t poll_event_th;
pthread_attr_t poll_event_attr;

void send_edid_data()
{
    int fd = -1;
    int ret;
    char path[] =
"/sys/devices/soc/ca0c000.qcom,cci/ca0c000.qcom,cci:toshiba,tc358840@0/tc358840_audio_data";

    fd = open(path, O_WRONLY, 0);
    if (fd < 0) {
        ALOGE("Unable open fd for file %s", path);
        return;
    }

    ret = write(fd, default_edid, sizeof(default_edid));

    close(fd);
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

void get_hdmi_status()
{
    read_data_from_fd(hdmi_in_audio_sys_path, &hdmi_conn_state);
    read_data_from_fd(hdmi_in_audio_state_sys_path, &hdmi_audio_state);
    read_data_from_fd(hdmi_in_audio_format_sys_path, &hdmi_audio_format);
    read_data_from_fd(hdmi_in_audio_sample_rate_sys_path, &hdmi_sample_rate);
    read_data_from_fd(hdmi_in_audio_channel_sys_path, &hdmi_num_channels);

    ALOGI("HDMI In state: %d, audio_state: %d, audio_format: %d,",
           hdmi_conn_state, hdmi_audio_state, hdmi_audio_format);
    ALOGI(" hdmi_sample_rate: %d, hdmi_num_channels: %d\n",
            hdmi_sample_rate, hdmi_num_channels);
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

    while(1) {

        fds.fd = sock_event_fd;
        fds.events = POLLIN;
        fds.revents = 0;
        i = poll(&fds, 1, -1);

        if (i > 0 && (fds.revents & POLLIN)) {
            count = recv(sock_event_fd, buffer, (64*1024), 0 );
            if (count > 0) {
                buffer[count] = '\0';
                j = 0;
                while(j < count) {
                    if (strncmp(&buffer[j], "DEVPATH=", 8) == 0) {
                        dev_path = &buffer[j+8];
                        j += 8;
                        continue;
                    } else if (strncmp(&buffer[j], "SWITCH_NAME=", 12) == 0) {
                        switch_name = &buffer[j+12];
                        j += 12;
                        continue;
                    } else if (strncmp(&buffer[j], "SWITCH_STATE=", 13) == 0) {
                        switch_state = &buffer[j+13];
                        j += 13;
                        continue;
                    }
                    j++;
                }
                ALOGI("devpath = %s, switch_name = %s \n",
                                         dev_path, switch_name);

                if (strncmp(hdmi_in_audio_dev_path, dev_path,
                                strlen(hdmi_in_audio_dev_path)) == 0) {
                    get_hdmi_status();
                } else if (strncmp(hdmi_in_audio_sample_rate_dev_path, dev_path,
                                strlen(hdmi_in_audio_sample_rate_dev_path)) == 0) {
                    get_hdmi_status();
                } else if (strncmp(hdmi_in_audio_state_dev_path, dev_path,
                                strlen(hdmi_in_audio_state_dev_path)) == 0) {
                    get_hdmi_status();
                } else if (strncmp(hdmi_in_audio_channel_dev_path, dev_path,
                                strlen(hdmi_in_audio_channel_dev_path)) == 0) {
                    get_hdmi_status();
                }
            }
        } else {
            ALOGD("NO Data\n");
        }
    }
}

int main()
{
    ALOGI("hdmi-in event test\n");

    pthread_attr_init(&poll_event_attr);
    pthread_attr_setdetachstate(&poll_event_attr, PTHREAD_CREATE_JOINABLE);
    poll_event_init();
    pthread_create(&poll_event_th, &poll_event_attr,
                       (void *) listen_uevent, NULL);
    get_hdmi_status();

    /* Enable once kernel side write is proper
    send_edid_data();*/
    pthread_join(poll_event_th, NULL);
    ALOGI("hdmi-in event test exit\n");
    return 0;
}
