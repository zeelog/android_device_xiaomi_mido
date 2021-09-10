/*
* Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
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
#include <getopt.h>
#include <pthread.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <utils/Log.h>
#include <math.h>

#include <cutils/list.h>
#include "qahw_api.h"
#include "qahw_defs.h"

static int sock_event_fd = -1;

pthread_t data_event_th = -1;
pthread_attr_t data_event_attr;

typedef struct tlb_hdmi_config {
    int hdmi_conn_state;
    int hdmi_audio_state;
    int hdmi_sample_rate;
    int hdmi_num_channels;
    int hdmi_data_format;
} tlb_hdmi_config_t;

const char tlb_hdmi_in_audio_sys_path[] =
"/sys/devices/virtual/switch/hpd_state/state";
const char tlb_hdmi_in_audio_dev_path[] = "/devices/virtual/switch/hpd_state";
const char tlb_hdmi_in_audio_state_sys_path[] =
"/sys/devices/virtual/switch/audio_state/state";
const char tlb_hdmi_in_audio_state_dev_path[] =
"/devices/virtual/switch/audio_state";
const char tlb_hdmi_in_audio_sample_rate_sys_path[] =
"/sys/devices/virtual/switch/sample_rate/state";
const char tlb_hdmi_in_audio_sample_rate_dev_path[] =
"/devices/virtual/switch/sample_rate";
const char tlb_hdmi_in_audio_channel_sys_path[] =
"/sys/devices/virtual/switch/channels/state";
const char tlb_hdmi_in_audio_channel_dev_path[] =
"/devices/virtual/switch/channels";
const char tlb_hdmi_in_audio_format_sys_path[] =
"/sys/devices/virtual/switch/audio_format/state";
const char tlb_hdmi_in_audio_format_dev_path[] =
"/devices/virtual/switch/audio_format";

qahw_module_handle_t *primary_hal_handle = NULL;

FILE * log_file = NULL;
volatile bool stop_loopback = false;
volatile bool exit_process_thread = false;
static float loopback_gain = 1.0;
const char *log_filename = NULL;

#define TRANSCODE_LOOPBACK_SOURCE_PORT_ID 0x4C00
#define TRANSCODE_LOOPBACK_SINK_PORT_ID 0x4D00

#define MAX_MODULE_NAME_LENGTH  100

#define DEV_NODE_CHECK(node_name,node_id) strncmp(node_name,node_id,strlen(node_name))

/* Function declarations */
void usage();
int poll_data_event_init();

typedef enum source_port_type {
    SOURCE_PORT_NONE,
    SOURCE_PORT_HDMI,
    SOURCE_PORT_SPDIF,
    SOURCE_PORT_MIC,
    SOURCE_PORT_BT
} source_port_type_t;

typedef enum source_port_state {
    SOURCE_PORT_INACTIVE=0,
    SOURCE_PORT_ACTIVE,
    SOURCE_PORT_CONFIG_CHANGED
} source_port_state_t;

typedef struct source_port_config {
    source_port_type_t source_port_type;
    source_port_state_t source_port_state;
    union {
        tlb_hdmi_config_t hdmi_in_port_config;
    } port_config;
} source_port_config_t;
typedef struct trnscode_loopback_config {
    qahw_module_handle_t *hal_handle;
    audio_devices_t devices;
    struct audio_port_config source_config;
    struct audio_port_config sink_config;
    audio_patch_handle_t patch_handle;
    source_port_config_t source_port_config;
} transcode_loopback_config_t;

transcode_loopback_config_t g_trnscode_loopback_config;

static int poll_data_event_exit()
{
   close(sock_event_fd);
}

void break_signal_handler(int signal __attribute__((unused)))
{
   stop_loopback = true;
}

int poll_data_event_init()
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

void init_transcode_loopback_config(transcode_loopback_config_t **p_transcode_loopback_config)
{
    fprintf(log_file,"\nInitializing global transcode loopback config\n");
    g_trnscode_loopback_config.hal_handle = NULL;

    audio_devices_t out_device = AUDIO_DEVICE_OUT_SPEAKER; // Get output device mask from connected device
    audio_devices_t in_device = AUDIO_DEVICE_IN_HDMI;

    g_trnscode_loopback_config.devices = (out_device | in_device);

    /* Patch source port config init */
    g_trnscode_loopback_config.source_config.id = TRANSCODE_LOOPBACK_SOURCE_PORT_ID;
    g_trnscode_loopback_config.source_config.role = AUDIO_PORT_ROLE_SOURCE;
    g_trnscode_loopback_config.source_config.type = AUDIO_PORT_TYPE_DEVICE;
    g_trnscode_loopback_config.source_config.config_mask =
                        (AUDIO_PORT_CONFIG_ALL ^ AUDIO_PORT_CONFIG_GAIN);
    g_trnscode_loopback_config.source_config.sample_rate = 48000;
    g_trnscode_loopback_config.source_config.channel_mask =
                        AUDIO_CHANNEL_OUT_STEREO; // Using OUT as this is digital data and not mic capture
    g_trnscode_loopback_config.source_config.format = AUDIO_FORMAT_PCM_16_BIT;
    /*TODO: add gain */
    g_trnscode_loopback_config.source_config.ext.device.hw_module =
                        AUDIO_MODULE_HANDLE_NONE;
    g_trnscode_loopback_config.source_config.ext.device.type = in_device;

    /* Patch sink port config init */
    g_trnscode_loopback_config.sink_config.id = TRANSCODE_LOOPBACK_SINK_PORT_ID;
    g_trnscode_loopback_config.sink_config.role = AUDIO_PORT_ROLE_SINK;
    g_trnscode_loopback_config.sink_config.type = AUDIO_PORT_TYPE_DEVICE;
    g_trnscode_loopback_config.sink_config.config_mask =
                            (AUDIO_PORT_CONFIG_ALL ^ AUDIO_PORT_CONFIG_GAIN);
    g_trnscode_loopback_config.sink_config.sample_rate = 48000;
    g_trnscode_loopback_config.sink_config.channel_mask =
                             AUDIO_CHANNEL_OUT_STEREO;
    g_trnscode_loopback_config.sink_config.format = AUDIO_FORMAT_PCM_16_BIT;

    g_trnscode_loopback_config.sink_config.ext.device.hw_module =
                            AUDIO_MODULE_HANDLE_NONE;
    g_trnscode_loopback_config.sink_config.ext.device.type = out_device;

    /* Init patch handle */
    g_trnscode_loopback_config.patch_handle = AUDIO_PATCH_HANDLE_NONE;

    memset(&g_trnscode_loopback_config.source_port_config,0,sizeof(source_port_config_t));
    g_trnscode_loopback_config.source_port_config.source_port_type = SOURCE_PORT_HDMI;
    g_trnscode_loopback_config.source_port_config.source_port_state = SOURCE_PORT_INACTIVE;

    poll_data_event_init();
    *p_transcode_loopback_config = &g_trnscode_loopback_config;

    fprintf(log_file,"\nDone Initializing global transcode loopback config\n");
}

void deinit_transcode_loopback_config()
{
    g_trnscode_loopback_config.hal_handle = NULL;

    g_trnscode_loopback_config.devices = AUDIO_DEVICE_NONE;
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

int actual_channels_from_audio_infoframe(int infoframe_channels)
{
    if (infoframe_channels > 0 && infoframe_channels < 8) {
      /* refer CEA-861-D Table 17 Audio InfoFrame Data Byte 1 */
        return (infoframe_channels+1);
    }
    fprintf(log_file,"\nInfoframe channels 0, need to get from stream, returning default 2\n");
    return 2;
}

int read_and_set_source_config(source_port_type_t source_port_type,
                               struct audio_port_config *dest_port_config)
{
    int rc=0;
    tlb_hdmi_config_t hdmi_config = {0};
    transcode_loopback_config_t *transcode_loopback_config = &g_trnscode_loopback_config;
    transcode_loopback_config->source_port_config.source_port_state = SOURCE_PORT_INACTIVE;
    switch(source_port_type)
    {
        case SOURCE_PORT_HDMI :
            read_data_from_fd(tlb_hdmi_in_audio_sys_path, &hdmi_config.hdmi_conn_state);
            read_data_from_fd(tlb_hdmi_in_audio_state_sys_path,
                              &hdmi_config.hdmi_audio_state);
            read_data_from_fd(tlb_hdmi_in_audio_sample_rate_sys_path,
                              &hdmi_config.hdmi_sample_rate);
            read_data_from_fd(tlb_hdmi_in_audio_channel_sys_path,
                              &hdmi_config.hdmi_num_channels);
            read_data_from_fd(tlb_hdmi_in_audio_format_sys_path,
                              &hdmi_config.hdmi_data_format);
        break;
        default :
            fprintf(log_file,"\nUnsupported port type, cannot set configuration\n");
            rc = -1;
        break;
    }

    hdmi_config.hdmi_num_channels = actual_channels_from_audio_infoframe(hdmi_config.hdmi_num_channels);

    if(hdmi_config.hdmi_data_format) {
        if(!( hdmi_config.hdmi_num_channels == 2 || hdmi_config.hdmi_num_channels == 8 )) {
                transcode_loopback_config->source_port_config.source_port_state = SOURCE_PORT_INACTIVE;
                return rc;
            }
    }
    dest_port_config->sample_rate = hdmi_config.hdmi_sample_rate;
    switch(hdmi_config.hdmi_num_channels) {
    case 2 :
        dest_port_config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
        break;
    case 3 :
        dest_port_config->channel_mask = AUDIO_CHANNEL_OUT_2POINT1;
        break;
    case 4 :
        dest_port_config->channel_mask = AUDIO_CHANNEL_OUT_QUAD;
        break;
    case 5 :
        dest_port_config->channel_mask = AUDIO_CHANNEL_OUT_PENTA;
        break;
    case 6 :
        dest_port_config->channel_mask = AUDIO_CHANNEL_OUT_5POINT1;
        break;
    case 7 :
        dest_port_config->channel_mask = AUDIO_CHANNEL_OUT_6POINT1;
        break;
    case 8 :
        dest_port_config->channel_mask = AUDIO_CHANNEL_OUT_7POINT1;
        break;
    default :
        fprintf(log_file,"\nUnsupported number of channels in source port %d\n",
                hdmi_config.hdmi_num_channels);
        rc = -1;
        break;
    }
    switch(hdmi_config.hdmi_data_format) {
    case 0 :
        // TODO : HDMI driver detecting as 0 for compressed also as of now
        //dest_port_config->format = AUDIO_FORMAT_AC3;
        dest_port_config->format = AUDIO_FORMAT_PCM_16_BIT;
        break;
    case 1 :
        dest_port_config->format = AUDIO_FORMAT_AC3;
        break;
    default :
        fprintf(log_file,"\nUnsupported data format in source port %d\n",
                hdmi_config.hdmi_data_format);
        rc = -1;
        break;
    }

    fprintf(log_file,"\nExisting HDMI In state: %d, audio_state: %d, samplerate: %d, channels: %d, format: %d\n",
            transcode_loopback_config->source_port_config.port_config.hdmi_in_port_config.hdmi_conn_state, transcode_loopback_config->source_port_config.port_config.hdmi_in_port_config.hdmi_audio_state,
            transcode_loopback_config->source_port_config.port_config.hdmi_in_port_config.hdmi_sample_rate, transcode_loopback_config->source_port_config.port_config.hdmi_in_port_config.hdmi_num_channels,
            transcode_loopback_config->source_port_config.port_config.hdmi_in_port_config.hdmi_data_format);

    fprintf(log_file,"\nSource port connection_state: %d, audio_state: %d, samplerate: %d, channels: %d, format: %d\n",
            hdmi_config.hdmi_conn_state, hdmi_config.hdmi_audio_state,
            hdmi_config.hdmi_sample_rate, hdmi_config.hdmi_num_channels,
            hdmi_config.hdmi_data_format);

    if( rc == 0 ) {
        if(hdmi_config.hdmi_audio_state)
        {
            if(memcmp(&hdmi_config,&(transcode_loopback_config->source_port_config.port_config.hdmi_in_port_config),sizeof(tlb_hdmi_config_t))) {
                transcode_loopback_config->source_port_config.source_port_state = SOURCE_PORT_CONFIG_CHANGED;
            } else {
                    transcode_loopback_config->source_port_config.source_port_state = SOURCE_PORT_ACTIVE;
            }
        } else {
                transcode_loopback_config->source_port_config.source_port_state = SOURCE_PORT_INACTIVE;
        }
        memcpy(&(transcode_loopback_config->source_port_config.port_config.hdmi_in_port_config),&hdmi_config,sizeof(tlb_hdmi_config_t));
    }
    return rc;
}

void stop_transcode_loopback(
            transcode_loopback_config_t *transcode_loopback_config)
{
    fprintf(log_file,"\nStopping current loopback session\n");
    if(transcode_loopback_config->patch_handle != AUDIO_PATCH_HANDLE_NONE)
        qahw_release_audio_patch(transcode_loopback_config->hal_handle,
                                 transcode_loopback_config->patch_handle);
    transcode_loopback_config->patch_handle = AUDIO_PATCH_HANDLE_NONE;
}

int create_run_transcode_loopback(
            transcode_loopback_config_t *transcode_loopback_config)
{
    int rc=0;
    qahw_module_handle_t *module_handle = transcode_loopback_config->hal_handle;


    fprintf(log_file,"\nCreating audio patch\n");
    if (transcode_loopback_config->patch_handle != AUDIO_PATCH_HANDLE_NONE) {
        fprintf(log_file,"\nPatch already existing, release the patch before opening a new patch\n");
        return rc;
    }
    rc = qahw_create_audio_patch(module_handle,
                        1,
                        &transcode_loopback_config->source_config,
                        1,
                        &transcode_loopback_config->sink_config,
                        &transcode_loopback_config->patch_handle);
    fprintf(log_file,"\nCreate patch returned %d\n",rc);
    if(!rc) {
        struct audio_port_config sink_gain_config;
        /* Convert loopback gain to millibels */
        int loopback_gain_in_millibels = 2000 * log10(loopback_gain);
        sink_gain_config.gain.index = 0;
        sink_gain_config.gain.mode = AUDIO_GAIN_MODE_JOINT;
        sink_gain_config.gain.channel_mask = 1;
        sink_gain_config.gain.values[0] = loopback_gain_in_millibels;
        sink_gain_config.id = transcode_loopback_config->sink_config.id;
        sink_gain_config.role = transcode_loopback_config->sink_config.role;
        sink_gain_config.type = transcode_loopback_config->sink_config.type;
        sink_gain_config.config_mask = AUDIO_PORT_CONFIG_GAIN;

        (void)qahw_set_audio_port_config(transcode_loopback_config->hal_handle,
                    &sink_gain_config);
    }

    return rc;
}

static qahw_module_handle_t *load_hal(audio_devices_t dev)
{
    if (primary_hal_handle == NULL) {
        primary_hal_handle = qahw_load_module(QAHW_MODULE_ID_PRIMARY);
        if (primary_hal_handle == NULL) {
            fprintf(stderr,"failure in Loading primary HAL\n");
            goto exit;
        }
    }

exit:
    return primary_hal_handle;
}

/*
* this function unloads all the loaded hal modules so this should be called
* after all the stream playback are concluded.
*/
static int unload_hals(void) {
    if (primary_hal_handle) {
        qahw_unload_module(primary_hal_handle);
        primary_hal_handle = NULL;
    }
    return 1;
}


void source_data_event_handler(transcode_loopback_config_t *transcode_loopback_config)
{
    int status =0;
    source_port_type_t source_port_type = transcode_loopback_config->source_port_config.source_port_type;

    if (source_port_type == SOURCE_PORT_HDMI) {
        status = read_and_set_source_config(source_port_type,&transcode_loopback_config->source_config);
        if (status) {
            fprintf(log_file,"\nFailure in source port configuration with status: %d\n", status);
            return;
        }
    } else {
        transcode_loopback_config->source_port_config.source_port_state = SOURCE_PORT_CONFIG_CHANGED;
    }

    fprintf(log_file,"\nSource port state : %d\n", transcode_loopback_config->source_port_config.source_port_state);

    if(transcode_loopback_config->source_port_config.source_port_state == SOURCE_PORT_CONFIG_CHANGED) {
        fprintf(log_file,"\nAudio state changed, Create and start transcode loopback session begin\n");
        stop_transcode_loopback(transcode_loopback_config);
        status = create_run_transcode_loopback(transcode_loopback_config);
        if(status)
        {
            fprintf(log_file,"\nCreate audio patch failed with status %d\n",status);
            stop_transcode_loopback(transcode_loopback_config);
        }
    } else if(transcode_loopback_config->source_port_config.source_port_state == SOURCE_PORT_INACTIVE) {
        stop_transcode_loopback(transcode_loopback_config);
    }
}

void process_loopback_data(void *ptr)
{
    char buffer[64*1024];
    struct pollfd fds;
    int i, count, status;
    int j;
    char *dev_path = NULL;
    char *switch_state = NULL;
    char *switch_name = NULL;
    transcode_loopback_config_t *transcode_loopback_config = &g_trnscode_loopback_config;

    fprintf(log_file,"\nEvent thread loop\n");
    source_data_event_handler(transcode_loopback_config);
    while (!stop_loopback) {

        fds.fd = sock_event_fd;
        fds.events = POLLIN;
        fds.revents = 0;
        /* poll wait time modified to wait forever */
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

                if (dev_path == NULL) {
                    fprintf(log_file, "NULL dev_path!");
                    continue;
                }

                if ((dev_path != NULL) && (switch_name != NULL))
                    fprintf(log_file,"devpath = %s, switch_name = %s \n",dev_path, switch_name);

                if((DEV_NODE_CHECK(tlb_hdmi_in_audio_dev_path, dev_path) == 0)  || (DEV_NODE_CHECK(tlb_hdmi_in_audio_sample_rate_dev_path, dev_path) == 0)
                || (DEV_NODE_CHECK(tlb_hdmi_in_audio_state_dev_path, dev_path) == 0)
                || (DEV_NODE_CHECK(tlb_hdmi_in_audio_channel_dev_path, dev_path) == 0)
                || (DEV_NODE_CHECK(tlb_hdmi_in_audio_format_dev_path, dev_path) == 0)) {
                    source_data_event_handler(transcode_loopback_config);
                }
            }
        } else {
            ALOGD("NO Data\n");
        }
    }
    fprintf(log_file,"\nEvent thread loop exit\n");

    stop_transcode_loopback(transcode_loopback_config);

    fprintf(log_file,"\nStop transcode loopback done\n");

    exit_process_thread = true;
}

void set_device(uint32_t source_device, uint32_t sink_device)
{
    transcode_loopback_config_t *transcode_loopback_config = &g_trnscode_loopback_config;

    transcode_loopback_config->sink_config.ext.device.type = sink_device;
    transcode_loopback_config->source_config.ext.device.type = source_device;

    switch (source_device) {
        case AUDIO_DEVICE_IN_SPDIF:
            g_trnscode_loopback_config.source_port_config.source_port_type = SOURCE_PORT_SPDIF;
            break;
        case AUDIO_DEVICE_IN_BLUETOOTH_A2DP:
            g_trnscode_loopback_config.source_port_config.source_port_type = SOURCE_PORT_BT;
            break;
        case AUDIO_DEVICE_IN_LINE:
            g_trnscode_loopback_config.source_port_config.source_port_type = SOURCE_PORT_MIC;
            break;
        case AUDIO_DEVICE_IN_HDMI:
        default:
            g_trnscode_loopback_config.source_port_config.source_port_type = SOURCE_PORT_HDMI;
            break;
    }
}

int main(int argc, char *argv[]) {

    int status = 0;
    uint32_t play_duration_in_seconds = 600,play_duration_elapsed_msec = 0,play_duration_in_msec = 0, sink_device = 2, volume_in_millibels = 0;
    uint32_t source_device = AUDIO_DEVICE_IN_HDMI;
    source_port_type_t source_port_type = SOURCE_PORT_NONE;
    log_file = stdout;
    transcode_loopback_config_t    *transcode_loopback_config = NULL;
    transcode_loopback_config_t *temp = NULL;
    char param[100] = {0};

    struct option long_options[] = {
        /* These options set a flag. */
        {"sink-device", required_argument,    0, 'o'},
        {"source-device", required_argument,    0, 'i'},
        {"play-duration",  required_argument,    0, 'p'},
        {"play-volume",  required_argument,    0, 'v'},
        {"help",          no_argument,          0, 'h'},
        {0, 0, 0, 0}
    };

    int opt = 0;
    int option_index = 0;

    while ((opt = getopt_long(argc,
                              argv,
                              "-o:i:p:v:h",
                              long_options,
                              &option_index)) != -1) {

        fprintf(log_file, "for argument %c, value is %s\n", opt, optarg);

        switch (opt) {
        case 'o':
            sink_device = atoll(optarg);
            break;
        case 'i':
            source_device = atoll(optarg);
            break;
        case 'p':
            play_duration_in_seconds = atoi(optarg);
            break;
        case 'v':
            loopback_gain = atof(optarg);
            break;
        case 'h':
        default :
            usage();
            return 0;
            break;
        }
    }

    fprintf(log_file, "source %#x sink %#x\n", source_device, sink_device);
    fprintf(log_file,"\nTranscode loopback test begin\n");
    if (play_duration_in_seconds < 0 | play_duration_in_seconds > 360000) {
            fprintf(log_file,
                    "\nPlayback duration %d invalid or unsupported(range : 1 to 360000, defaulting to 600 seconds )\n",
                    play_duration_in_seconds);
            play_duration_in_seconds = 600;
    }
    play_duration_in_msec = play_duration_in_seconds * 1000;

    /* Register the SIGINT to close the App properly */
    if (signal(SIGINT, break_signal_handler) == SIG_ERR) {
        fprintf(log_file, "Failed to register SIGINT:%d\n",errno);
        fprintf(stderr, "Failed to register SIGINT:%d\n",errno);
    }

    /* Initialize global transcode loopback struct */
    init_transcode_loopback_config(&temp);
    transcode_loopback_config = &g_trnscode_loopback_config;

    /* Set devices */
    set_device(source_device, sink_device);

    /* Load HAL */
    fprintf(log_file,"\nLoading HAL for loopback usecase begin\n");
    primary_hal_handle = load_hal(transcode_loopback_config->devices);
    if (primary_hal_handle == NULL) {
        fprintf(log_file,"\n Failure in Loading HAL, exiting\n");
        /* Set the exit_process_thread flag for exiting test */
        exit_process_thread = true;
        goto exit_transcode_loopback_test;
    }

    if (sink_device == AUDIO_DEVICE_OUT_BLUETOOTH_A2DP) {
        snprintf(param, sizeof(param), "%s=%d", "connect", sink_device);
        qahw_set_parameters(primary_hal_handle, param);
    }

    transcode_loopback_config->hal_handle = primary_hal_handle;
    fprintf(log_file,"\nLoading HAL for loopback usecase done\n");

    pthread_attr_init(&data_event_attr);
    fprintf(log_file,"\nData thread init done\n");
    pthread_attr_setdetachstate(&data_event_attr, PTHREAD_CREATE_JOINABLE);
    fprintf(log_file,"\nData thread setdetachstate done\n");

    fprintf(log_file,"\nData thread create\n");
    pthread_create(&data_event_th, &data_event_attr,
                       (void *) process_loopback_data, NULL);
    fprintf(log_file,"\nMain thread loop\n");
    while(!stop_loopback) {
        usleep(5000*1000);
        play_duration_elapsed_msec += 5000;
        if(play_duration_in_msec <= play_duration_elapsed_msec)
        {
            stop_loopback = true;
            fprintf(log_file,"\nElapsed set playback duration %d seconds, exiting test\n",play_duration_in_msec/1000);
            break;
        }
    }
    fprintf(log_file,"\nMain thread loop exit\n");

exit_transcode_loopback_test:
    poll_data_event_exit();
    /* Wait for process thread to exit */
    while (!exit_process_thread) {
        usleep(10*1000);
    }
    fprintf(log_file,"\nJoining loopback thread\n");
    status = pthread_join(data_event_th, NULL);
    fprintf(log_file, "\n thread join completed, status:%d \n", status);
    exit_process_thread = false;

    fprintf(log_file,"\nUnLoading HAL for loopback usecase begin\n");
    unload_hals();
    fprintf(log_file,"\nUnLoading HAL for loopback usecase end\n");

    deinit_transcode_loopback_config();
    transcode_loopback_config = NULL;

    fprintf(log_file,"\nTranscode loopback test end\n");
    return 0;
}

void usage()
{
    fprintf(log_file,"\nUsage : trans_loopback_test -p <duration_in_seconds> -d <sink_device_id> -v <loopback_volume(range 0 to 4.0)>\n");
    fprintf(log_file,"\nExample to play for 1 minute on speaker device with volume unity: trans_loopback_test -p 60 -d 2 -v 1.0\n");
    fprintf(log_file,"\nExample to play for 5 minutes on headphone device: trans_loopback_test -p 300 -d 8\n");
    fprintf(log_file,"\nHelp : trans_loopback_test -h\n");
 }
