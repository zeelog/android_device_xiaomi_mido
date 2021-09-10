/*
* Copyright (c) 2019, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
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

/* Test app for voice call */

#include "qahw_voice_test.h"

#define ID_RIFF 0x46464952
#define ID_WAVE 0x45564157
#define ID_FMT  0x20746d66
#define ID_DATA 0x61746164

#define FORMAT_PCM 1
#define WAV_HEADER_LENGTH_MAX 128
#define FORMAT_DESCRIPTOR_SIZE 12
#define SUBCHUNK1_SIZE(x) ((8) + (x))
#define SUBCHUNK2_SIZE 8

voice_stream_config stream_params;
volatile bool stop = false;
void *context = NULL;

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

static void init_stream(void) {
    stream_params.vsid = "11C05000";
    stream_params.qahw_mod_handle = NULL;
    stream_params.call_length = -1; /*infinite*/
    stream_params.multi_call = 1;
    stream_params.output_device[0] = AUDIO_DEVICE_OUT_WIRED_HEADSET;
    stream_params.output_device[1] = AUDIO_DEVICE_IN_BUILTIN_MIC;
    stream_params.in_call_rec = false;
    stream_params.in_call_playback = false;
    stream_params.hpcm = false;
    stream_params.hpcm_tp = 2;
    stream_params.tp_dir = 0;
    stream_params.rec_file = "/data/default_rec.wav";
    stream_params.playback_file = NULL;
    stream_params.vol = .75;
    stream_params.mute = false;
    stream_params.mute_dir = 0;
    stream_params.tty_mode = 0;
    stream_params.dtmf_gen_enable = 0;
    stream_params.dtmf_freq_low = 697;
    stream_params.dtmf_freq_high =  1209;
    stream_params.dtmf_gain = 100;
}

void usage() {
    printf(" \n Command \n");
    printf(" \n hal_voice_test <options>   - starts voice call\n");
    printf(" \n Options\n");
    printf(" -i  --vsid <vsid>                   - vsid to use sim1<297816064> sim2<29965107>.\n");
    printf(" -d  --device <decimal value>        - see system/media/audio/include/system/audio.h for device values\n");
    printf(" -l  --length <call length>          - call length in sec.\n");
    printf(" -m  --multi_call <number of calls>  - number of calls to make.\n");
    printf(" -r  --in_call_rec <filename to record to> -t  - tp_dir <0 = DL, 1 = UL, 2 = BOTH >\n");
    printf(" -p  --in_call_playback <filename to play from>  play audio to voice call\n");
    printf(" -v  --vol <val>               - volume.\n");
    printf(" -u  --mute <dir>              - <dir 0= tx, 1 = rx> .\n");
    printf(" -c  --dtmf_gen                                     .\n");
    printf(" -y  --tty_mode                - <MODE_OFF = 0, MODE_FULL = 1, MODE_VCO  = 2, MODE_HCO = 3\n");
}

void stop_signal_handler(int signal __unused) {
    stop = true;
}

static void qti_audio_server_death_notify_cb(void *ctxt __unused) {
    fprintf(stderr, "qas died\n");
    stop = true;
}

void *rec_start(void *thread_param) {
    uint32_t rc = 0;
    voice_stream_config *params = (voice_stream_config *)thread_param;
    qahw_module_handle_t *qahw_mod_handle = params->qahw_mod_handle;
    qahw_stream_handle_t *in_handle = NULL;
    uint32_t num_dev = 1;
    audio_devices_t in_device[1] = { AUDIO_DEVICE_IN_BUILTIN_MIC };
    struct qahw_stream_attributes attr;
    qahw_buffer_t in_buf;
    int data_sz = 0;
    ssize_t bytes_read = -1;

    fprintf(stderr, "%s starting rec thread\n", __func__);
    if (qahw_mod_handle == NULL) {
        fprintf(stderr, " qahw_load_module failed\n");
        pthread_exit(0);
    }
    if(params->in_call_rec) {
        fprintf(stderr, " setting in call record params\n");
        switch (params->tp_dir) {
        case 0:
            attr.type = QAHW_AUDIO_CAPTURE_VOICE_CALL_RX;
            break;
        case 1:
            attr.type = QAHW_AUDIO_CAPTURE_VOICE_CALL_TX;
            break;
        default:
            fprintf(stderr, " invalid tp direction");
            pthread_exit(0);
            break;
        }
            attr.attr.audio.config.sample_rate = 48000;
    }
    if(params->hpcm) {
        fprintf(stderr, "setting host pcm params\n");
        switch(params->hpcm_tp) {
            case QAHW_HPCM_TAP_POINT_RX:
                attr.type = QAHW_AUDIO_HOST_PCM_RX;
                break;
            case QAHW_HPCM_TAP_POINT_TX:
                attr.type = QAHW_AUDIO_HOST_PCM_TX;
                break;
            default:
                fprintf(stderr, "unsupported tp %d\n", params->hpcm_tp);
                pthread_exit(0);
                break;
        }
        attr.attr.audio.config.sample_rate = 8000;
    }
    attr.direction = QAHW_STREAM_INPUT;
    attr.attr.audio.config.format = AUDIO_FORMAT_PCM_16_BIT;

    rc = qahw_stream_open(qahw_mod_handle,
                          attr,
                          num_dev,
                          in_device,
                          0,
                          NULL,
                          NULL,
                          NULL,
                          &(in_handle));
    if (rc) {
        fprintf(stderr, " open input device failed!\n");
        pthread_exit(0);
    }

    /* Get buffer size to get upper bound on data to read from the HAL */
    size_t buffer_size;
    rc = qahw_stream_get_buffer_size(in_handle, &buffer_size, NULL);
    char *buffer = (char *)calloc(1, buffer_size);
    size_t written_size;
    int bps = 16;

    if (buffer == NULL) {
        fprintf(stderr, "calloc failed!!, handle(%d)\n", in_handle);
        pthread_exit(0);
    }
    if (params->rec_file == NULL) {
        fprintf(stderr, "no record stream provided\n", in_handle);
        pthread_exit(0);
        return NULL;
    }
    FILE *fd = fopen(params->rec_file, "w");
    if (fd == NULL) {
        fprintf(stderr, "File open failed \n");
        free(buffer);
        pthread_exit(0);
    }
    struct wav_header hdr;
    hdr.riff_id = ID_RIFF;
    hdr.riff_sz = 0;
    hdr.riff_fmt = ID_WAVE;
    hdr.fmt_id = ID_FMT;
    hdr.fmt_sz = 16;
    hdr.audio_format = FORMAT_PCM;
    hdr.num_channels = 1;
    hdr.sample_rate = attr.attr.audio.config.sample_rate;
    hdr.byte_rate = hdr.sample_rate * hdr.num_channels * (bps / 8);
    hdr.block_align = hdr.num_channels * (bps / 8);
    hdr.bits_per_sample = bps;
    hdr.data_id = ID_DATA;
    hdr.data_sz = 0;
    fwrite(&hdr, 1, sizeof(hdr), fd);

    memset(&in_buf, 0, sizeof(qahw_buffer_t));
    fprintf(stderr, "file %s opened for write", params->rec_file);
    while (true && !stop) {
        in_buf.buffer = buffer;
        in_buf.size = buffer_size;

        bytes_read = qahw_stream_read(in_handle, &in_buf);

        written_size = fwrite(in_buf.buffer, 1, buffer_size, fd);
        if (written_size < buffer_size) {
            fprintf(stderr, "Error in fwrite\n");
            break;
        }
        data_sz += buffer_size;
    }
    fprintf(stderr, "rec ended\n");
    /* update lengths in header */
    hdr.data_sz = data_sz;
    hdr.riff_sz = data_sz + 44 - 8;
    fseek(fd, 0, SEEK_SET);
    fwrite(&hdr, 1, sizeof(hdr), fd);
    free(buffer);
    fclose(fd);
    fd = NULL;
    fprintf(stderr, " closing input, handle(%d)", in_handle);

    /* Close input stream and device. */
    rc = qahw_stream_standby(in_handle);
    if (rc) {
        fprintf(stderr, "out standby failed %d, handle(%d)\n", rc, in_handle);
    }

    rc = qahw_stream_close(in_handle);
    if (rc) {
        fprintf(stderr, "could not close input stream %d, handle(%d)\n", rc, in_handle);
    }

    /* Print instructions to access the file.
     * Caution: Below ADL log shouldnt be altered without notifying automation APT since it used for
     * automation testing
     */
    fprintf(stderr, "\n\n ADL: The audio recording has been saved to %s. Please use adb pull to get "
            "the file and play it using audacity. The audio data has the "
            "following characteristics:\n Sample rate: %i\n Format: %d\n "
            "Num channels: %i\n\n",
            params->rec_file, attr.attr.audio.config.sample_rate, attr.attr.audio.config.format, 1);
    pthread_exit(0);

    return NULL;
}

int get_wav_header_length(FILE *file_stream) {
    int subchunk_size = 0;
    int wav_header_len = 0;

    fseek(file_stream, 16, SEEK_SET);
    if (fread(&subchunk_size, 4, 1, file_stream) != 1) {
        fprintf(stderr, "Unable to read subchunk:\n");
        exit(1);
    }
    if (subchunk_size < 16) {
        fprintf(stderr, "This is not a valid wav file \n");
    } else {
        wav_header_len = FORMAT_DESCRIPTOR_SIZE + SUBCHUNK1_SIZE(subchunk_size) + SUBCHUNK2_SIZE;
    }
    return wav_header_len;
}

void *playback_start(void *thread_param) {
    uint32_t rc = 0;
    voice_stream_config *params = (voice_stream_config *)thread_param;
    qahw_module_handle_t *qahw_mod_handle = params->qahw_mod_handle;
    qahw_stream_handle_t *out_handle = NULL;
    uint32_t num_dev = 1;
    audio_devices_t out_device[1] = { AUDIO_DEVICE_OUT_WIRED_HEADSET };
    struct qahw_stream_attributes attr;
    size_t bytes_wanted = 0;
    size_t write_length = 0;
    size_t bytes_remaining = 0;
    ssize_t bytes_written = 0;
    FILE *fp = NULL;
    size_t bytes_read = 0;
    qahw_buffer_t out_buf;
    char  *data_ptr = NULL;
    bool exit = false;
    bool read_complete_file = true;
    int wav_header_len;
    char header[WAV_HEADER_LENGTH_MAX] = { 0 };

    if (qahw_mod_handle == NULL) {
        fprintf(stderr, " qahw_load_module failed");
        pthread_exit(0);
    }

    attr.direction = QAHW_STREAM_OUTPUT;
    if(params->in_call_playback) {
        attr.type = QAHW_AUDIO_PLAYBACK_VOICE_CALL_MUSIC;
        attr.attr.audio.config.sample_rate = 48000;
        attr.attr.audio.config.format = AUDIO_FORMAT_PCM_16_BIT;
    }
    if(params->hpcm) {
        switch(params->hpcm_tp) {
            case QAHW_HPCM_TAP_POINT_RX:
                attr.type = QAHW_AUDIO_HOST_PCM_RX;
                break;
            case QAHW_HPCM_TAP_POINT_TX:
                attr.type = QAHW_AUDIO_HOST_PCM_TX;
                break;
            default:
                fprintf(stderr, "unsupported tp %d\n", params->hpcm_tp);
                pthread_exit(0);
                break;
        }
        attr.attr.audio.config.sample_rate = 8000;
        attr.attr.audio.config.format = AUDIO_FORMAT_PCM_16_BIT;
    }


    if (params->playback_file != NULL)
        fp = fopen(params->playback_file, "r");
    if (fp == NULL) {
        fprintf(stderr, "failed to open file %s\n", params->playback_file);
        pthread_exit(0);
    }
    /*
    * Read the wave header
    */
    if ((wav_header_len = get_wav_header_length(fp)) <= 0) {
        fprintf(stderr, "wav header length is invalid:%d\n", wav_header_len);
        pthread_exit(0);
    }
    fseek(fp, 0, SEEK_SET);
    rc = fread(header, wav_header_len, 1, fp);
    if (rc != 1) {
        fprintf(stderr, "Error fread failed\n");
        pthread_exit(0);
    }
    if (strncmp(header, "RIFF", 4) && strncmp(header + 8, "WAVE", 4)) {;
        fprintf(stderr, "Not a wave format\n");
        pthread_exit(0);
    }
    //memcpy (&stream_info->channels, &header[22], 2);
    memcpy(&attr.attr.audio.config.offload_info.sample_rate, &header[24], 4);
    memcpy(&attr.attr.audio.config.offload_info.bit_width, &header[34], 2);
    if (attr.attr.audio.config.offload_info.bit_width == 32)
        attr.attr.audio.config.offload_info.format = AUDIO_FORMAT_PCM_32_BIT;
    else if (attr.attr.audio.config.offload_info.bit_width == 24)
        attr.attr.audio.config.offload_info.format = AUDIO_FORMAT_PCM_24_BIT_PACKED;
    else
        attr.attr.audio.config.offload_info.format = AUDIO_FORMAT_PCM_16_BIT;

    attr.attr.audio.config.sample_rate = attr.attr.audio.config.offload_info.sample_rate;
    attr.attr.audio.config.format = attr.attr.audio.config.offload_info.format;

    rc = qahw_stream_open(qahw_mod_handle,
                          attr,
                          num_dev,
                          out_device,
                          0,
                          NULL,
                          NULL,
                          NULL,
                          &(out_handle));

    if (rc) {
        fprintf(stderr, " open output device failed!\n");
        pthread_exit(0);
    }
    rc = qahw_stream_get_buffer_size(out_handle ,NULL, &bytes_wanted);
    data_ptr = (char *)malloc(bytes_wanted);
    if (data_ptr == NULL) {
        fprintf(stderr, "failed to allocate data buffer\n");
        pthread_exit(0);
    }

    while (!exit && !stop) {
        if (!bytes_remaining) {
            bytes_read = fread(data_ptr, 1, bytes_wanted, fp);
            fprintf(stderr, "read bytes %zd\n", bytes_read);
            bytes_remaining = write_length = bytes_read;
        }

        bytes_written = bytes_remaining;
        memset(&out_buf, 0, sizeof(qahw_buffer_t));
        out_buf.buffer = data_ptr;
        out_buf.size = bytes_remaining;
        bytes_written = qahw_stream_write(out_handle, &out_buf);
        if (bytes_written <= 0) {
            fprintf(stderr, "write end %d", bytes_written);
            exit = true;
            continue;
        }
        bytes_remaining -= bytes_written;
    }

    fclose(fp);
    if (data_ptr)
        free(data_ptr);

    qahw_stream_close(out_handle);

    return NULL;
}

int main(int argc, char *argv[]) {

    uint32_t rc = 0;
    int opt = 0;
    int option_index = 0;
    qahw_stream_direction dir;
    int call_count = 0;
    int call_lenght = 0;
    pthread_t tid_rec;
    pthread_t tid_pb;

    init_stream();

    struct option long_options[] = {
        /* These options set a flag. */
        { "vsid",     required_argument,    0, 'i' },
        { "device",     required_argument,    0, 'd' },
        { "call_length",     required_argument,    0, 'l' },
        { "help",          no_argument,          0, 'h' },
        { "in_call_playback",  required_argument,  0, 'p' },
        { "in_call_rec",  required_argument,  0, 'r' },
        { "host_pcm",  no_argument,  0, 'b' },
        { "tp_dir",  required_argument,  0, 't' },
        { "file",  required_argument,  0, 'f' },
        { "hpcm_tp",  required_argument,  0, 'a' },
        { "vol",  required_argument,  0, 'v' },
        { "mute",  required_argument,  0, 'u' },
        { "tty_mode",  required_argument,  0, 'y' },
        { "dtmf_gen", no_argument,  0, 'c' },
        { 0, 0, 0, 0 }
    };

    while ((opt = getopt_long(argc,
                              argv,
                              "-v:d:l:m:p:r:t:f:a:b:h:i:u:y:c:",
                              long_options,
                              &option_index)) != -1) {

        fprintf(stderr, "for argument %c, value is %s\n", opt, optarg);

        switch (opt) {
        case 'i':
            stream_params.vsid = optarg;
            break;
        case 'd':
            stream_params.output_device[0] = atoll(optarg);
            break;
        case 'l':
            stream_params.call_length = atoll(optarg);
            break;
        case 'm':
            stream_params.multi_call = atoll(optarg);
            break;
        case 'p':
            stream_params.in_call_playback = true;
            stream_params.playback_file = optarg;
            break;
        case 'r':
            stream_params.in_call_rec = true;
            stream_params.rec_file = optarg;
            break;
        case 'b':
            stream_params.hpcm = true;
            break;
        case 'f':
            stream_params.playback_file = optarg;
            break;
        case 't':
            stream_params.tp_dir = atoll(optarg);
            break;
        case 'a':
            stream_params.hpcm_tp = atoll(optarg);
            break;
        case 'v':
            stream_params.vol = atof(optarg);
            break;
        case 'u':
            stream_params.mute_dir = atoll(optarg);
            stream_params.mute = true;
            break;
        case 'y':
            stream_params.tty_mode = atoll(optarg);
            break;
        case 'c':
            stream_params.dtmf_gen_enable = true;
            break;
        case 'h':
        default:
            usage();
            return 0;
        }
    }

    /* Register the SIGINT to close the App properly */
    if (signal(SIGINT, stop_signal_handler) == SIG_ERR)
        fprintf(stderr, "Failed to register SIGINT:%d\n", errno);

    /* Register the SIGTERM to close the App properly */
    if (signal(SIGTERM, stop_signal_handler) == SIG_ERR)
        fprintf(stderr, "Failed to register SIGTERM:%d\n", errno);

    qahw_register_qas_death_notify_cb((audio_error_callback)qti_audio_server_death_notify_cb, context);

    fprintf(stderr, "starting voice call\n");
    if ((stream_params.qahw_mod_handle = qahw_load_module(QAHW_MODULE_ID_PRIMARY)) == NULL) {
        fprintf(stderr, "failure in Loading primary HAL\n");
        goto exit;
    }

    struct qahw_stream_attributes attr;

    attr.type = QAHW_VOICE_CALL;
    attr.direction = QAHW_STREAM_INPUT_OUTPUT;
    attr.attr.voice.vsid = stream_params.vsid;
    stream_params.out_voice_handle = NULL;

    fprintf(stderr, "vsid is %s device is %d \n", attr.attr.voice.vsid, stream_params.output_device[0]);
    rc = qahw_stream_open(stream_params.qahw_mod_handle,
                          attr,
                          1,
                          stream_params.output_device,
                          0,
                          NULL,
                          NULL,
                          NULL,
                          &(stream_params.out_voice_handle));
    if (rc) {
        fprintf(stderr, "Could not open output stream.\n");
        goto unload;
    }
    /*set tty mode if needed*/
    if(stream_params.tty_mode) {
        qahw_param_payload tty;
        tty.tty_mode_params.mode = stream_params.tty_mode;
        rc = qahw_stream_set_parameters(stream_params.out_voice_handle,
                                        QAHW_PARAM_TTY_MODE, &tty);
    }
    while (stream_params.multi_call) {
        call_count++;
        rc = qahw_stream_start(stream_params.out_voice_handle);

        if (rc) {
            fprintf(stderr, "Could not start voice stream.\n");
            goto close_stream;
        }
        fprintf(stderr, "started voice call %d\n", call_count);
        /*set volume */
        struct qahw_volume_data vol;
        struct qahw_channel_vol vol_pair;

        vol_pair.channel = QAHW_CHANNEL_L;
        vol_pair.vol = stream_params.vol;
        vol.num_of_channels = 1;
        vol.vol_pair = &vol_pair;

        rc = qahw_stream_set_volume(stream_params.out_voice_handle, vol);
        if(rc){
            fprintf(stderr, "set vol failed rc %d!\n", rc);
        }
        call_lenght = stream_params.call_length;
        if (stream_params.in_call_rec) {
            fprintf(stderr, "\n Create %s in call record thread \n");
            rc = pthread_create(&tid_rec, NULL, rec_start, (void *)&stream_params);
            if (rc) {
                fprintf(stderr, "in call rec thread creation failed %d\n");
            }
        }
        if (stream_params.in_call_playback) {
            fprintf(stderr, "\n Create %s incall playback thread \n");
            rc = pthread_create(&tid_pb, NULL, playback_start, (void *)&stream_params);
            if (rc) {
                fprintf(stderr, "in call playback thread creation failed %d\n");
            }
        }
        if(stream_params.mute) {
           struct qahw_mute_data mute;
           mute.enable = true;
           mute.direction = stream_params.mute_dir;
           rc = qahw_stream_set_mute(stream_params.out_voice_handle, mute);
        }
        if(stream_params.dtmf_gen_enable) {
            qahw_param_payload dtmf;
            dtmf.dtmf_gen_params.low_freq = stream_params.dtmf_freq_low;
            dtmf.dtmf_gen_params.high_freq = stream_params.dtmf_freq_high;
            dtmf.dtmf_gen_params.gain = stream_params.dtmf_gain;
            dtmf.dtmf_gen_params.enable = true;
            rc = qahw_stream_set_parameters(stream_params.out_voice_handle,
                                            QAHW_PARAM_DTMF_GEN, &dtmf);
            /*let play for 50 ms*/
            usleep(50000000);
            dtmf.dtmf_gen_params.enable = false;
            rc = qahw_stream_set_parameters(stream_params.out_voice_handle,
                                            QAHW_PARAM_DTMF_GEN, &dtmf);

        }
        /*setup hpcm if needed*/
        if(stream_params.hpcm) {
            fprintf(stderr, "calling hpcm set param.\n");
            qahw_param_payload hpcm;
            hpcm.hpcm_params.tap_point = stream_params.hpcm_tp;
            hpcm.hpcm_params.direction = stream_params.tp_dir;
            rc = qahw_stream_set_parameters(stream_params.out_voice_handle,
                                        QAHW_PARAM_HPCM, &hpcm);

            switch(stream_params.tp_dir) {
                case QAHW_HPCM_DIRECTION_OUT:
                    fprintf(stderr, "\n Create %s hpcm playback thread \n");
                    rc = pthread_create(&tid_pb, NULL, playback_start,
                                        (void *)&stream_params);
                    break;
                case QAHW_HPCM_DIRECTION_IN:
                    fprintf(stderr, "\n Create %s hpcm record thread \n");
                    rc = pthread_create(&tid_rec, NULL, rec_start,
                                        (void *)&stream_params);
                    break;
                case QAHW_HPCM_DIRECTION_OUT_IN:
                    fprintf(stderr, "\n Create %s hpcm record thread \n");
                    rc = pthread_create(&tid_rec, NULL, rec_start,
                                        (void *)&stream_params);
                    fprintf(stderr, "\n Create %s hpcm playback thread \n");
                    rc = pthread_create(&tid_pb, NULL, playback_start,
                                        (void *)&stream_params);
                    break;
                default:
                    fprintf(stderr, "\n invalid HPCM direction  \n");
                    break;
            }
        }
        while (call_lenght) {
            usleep(1000000);
            call_lenght--;
        }
        stop = true;
        fprintf(stderr, "stoping call %d\n", call_count);
        rc = qahw_stream_stop(stream_params.out_voice_handle);
        stream_params.multi_call--;
        /*let session stop*/
        usleep(100000);
    }

 close_stream:
    fprintf(stderr, "closing voice stream\n");
    rc = qahw_stream_close(stream_params.out_voice_handle);

 unload:
    fprintf(stderr, "unloading hal\n");
    if (qahw_unload_module(stream_params.qahw_mod_handle) < 0) {
        fprintf(stderr, "failure in Un Loading primary HAL\n");
        return -1;
    }
    fprintf(stderr, "voice test ended\n");
 exit:
    return 0;
}
