/*
* Copyright (c) 2017, The Linux Foundation. All rights reserved.
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

/* effect test to be applied on HAL layer */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include "qahw_api.h"
#include "qahw_defs.h"
#include "qahw_effect_api.h"
#include "qahw_effect_audiosphere.h"
#include "qahw_effect_bassboost.h"
#include "qahw_effect_environmentalreverb.h"
#include "qahw_effect_equalizer.h"
#include "qahw_effect_presetreverb.h"
#include "qahw_effect_virtualizer.h"
#include "qahw_effect_visualizer.h"

#include "qahw_effect_test.h"

// lookup table of allowed effect commands
#define MAX_CMD_STR_SIZE 20
cmd_def_t cmds_lookup_table[EFFECT_MAX][TTY_CMD_MAX] = {
    { /* EFFECT_BASSBOOST */
        {"enable",   TTY_ENABLE,               NULL},
        {"disable",  TTY_DISABLE,              NULL},
        {"strength", TTY_BB_SET_STRENGTH,      "input bassboost strength value(0-1000):\n"},
        {"invalid",  TTY_INVALID,              NULL},
    },
    { /* EFFECT_VIRTUALIZER */
        {"enable",   TTY_ENABLE,               NULL},
        {"disable",  TTY_DISABLE,              NULL},
        {"strength", TTY_VT_SET_STRENGTH,      "input virtualizer strength value(0-1000):\n"},
        {"invalid",  TTY_INVALID,              NULL},
    },
    { /* EFFECT_EQUALIZER */
        {"enable",   TTY_ENABLE,               NULL},
        {"disable",  TTY_DISABLE,              NULL},
        {"preset",   TTY_EQ_SET_PRESET,        "select equalizer presets:\n(0-normal, 1-classical, 2-dance, 3-flat, 4-folk, 5-heavy metal, 6-hiphop, 7-jazz, 8-pop, 9-rock, 10-fx booster)\n"},
        {"custom",   TTY_EQ_SET_CUSTOM,        "customize equalizer settings:\n"},
    },
    { /* EFFECT_VISUALIZER */
        {"invalid",  TTY_INVALID,              NULL},
        {"invalid",  TTY_INVALID,              NULL},
        {"invalid",  TTY_INVALID,              NULL},
        {"invalid",  TTY_INVALID,              NULL},
    },
    { /* EFFECT_REVERB */
        {"enable",   TTY_ENABLE,               NULL},
        {"disable",  TTY_DISABLE,              NULL},
        {"preset",   TTY_RB_SET_PRESET,        "select reverb presets:\n(0-none, 1-small room, 2-medium room, 3-large room, 4-medium hall, 5-large hall, 6-plate)\n"},
        {"invalid",  TTY_INVALID,              NULL},
    },
    { /* EFFECT_AUDIOSPHERE */
        {"enable",   TTY_ENABLE,               NULL},
        {"disable",  TTY_DISABLE,              NULL},
        {"strength", TTY_ASPHERE_SET_STRENGTH, "input audiosphere strength value(0-1000):\n"},
        {"invalid",  TTY_INVALID,              NULL},
    },
};

thread_func_t effect_thread_funcs[EFFECT_MAX] = {
    &bassboost_thread_func,
    &virtualizer_thread_func,
    &equalizer_thread_func,
    &visualizer_thread_func,
    &reverb_thread_func,
    &asphere_thread_func,
};

const char * effect_str[EFFECT_MAX] = {
    "bassboost",
    "virtualizer",
    "equalizer",
    "visualizer",
    "reverb",
    "audiosphere",
};

// placing non-standard EQ stuff here rather than in header file
#define NUM_EQ_BANDS 5
const uint16_t qahw_equalizer_band_freqs[NUM_EQ_BANDS] = {60, 230, 910, 3600, 14000}; /* frequencies in HZ */

/* Handler to handle input command_thread_func signal */
void stop_effect_command_thread_handler(int signal __unused)
{
   pthread_exit(NULL);
}

/* THREAD BODY OF BASSBOOST */
void *bassboost_thread_func(void* data) {
    thread_data_t            *thr_ctxt = (thread_data_t *)data;
    qahw_effect_lib_handle_t lib_handle;
    qahw_effect_handle_t     effect_handle;
    qahw_effect_descriptor_t effect_desc;
    int32_t                  rc;
    int                      reply_data;
    uint32_t                 reply_size = sizeof(int);
    uint32_t array_size = sizeof(qahw_effect_param_t) + 2 * sizeof(int32_t);
    uint32_t      buf32[array_size];
    qahw_effect_param_t *values;

    pthread_mutex_lock(&thr_ctxt->mutex);
    while(!thr_ctxt->exit) {
        // suspend thread till signaled
        fprintf(stdout, "suspend effect thread\n");
        pthread_cond_wait(&thr_ctxt->loop_cond, &thr_ctxt->mutex);
        fprintf(stdout, "awake effect thread\n");

        switch(thr_ctxt->cmd) {
        case(EFFECT_LOAD_LIB):
            lib_handle = qahw_effect_load_library(QAHW_EFFECT_BASSBOOST_LIBRARY);
            break;
        case(EFFECT_GET_DESC):
            rc = qahw_effect_get_descriptor(lib_handle, SL_IID_BASSBOOST_UUID, &effect_desc);
            if (rc != 0) {
                fprintf(stderr, "effect_get_descriptor() returns %d\n", rc);
            }
            break;
        case(EFFECT_CREATE):
            rc = qahw_effect_create(lib_handle, SL_IID_BASSBOOST_UUID,
                                    thr_ctxt->io_handle, &effect_handle);
            if (rc != 0) {
                fprintf(stderr, "effect_create() returns %d\n", rc);
            }
            break;
        case(EFFECT_CMD):
            thr_ctxt->reply_size = (uint32_t *)&reply_size;
            thr_ctxt->reply_data = (void *)&reply_data;
            rc = qahw_effect_command(effect_handle, thr_ctxt->cmd_code,
                                     thr_ctxt->cmd_size, thr_ctxt->cmd_data,
                                     thr_ctxt->reply_size, thr_ctxt->reply_data);
            if (rc != 0) {
                fprintf(stderr, "effect_command() returns %d\n", rc);
            }
            if (thr_ctxt->default_flag && (thr_ctxt->cmd_code == QAHW_EFFECT_CMD_ENABLE)) {
                if (thr_ctxt->default_value == -1)
                    thr_ctxt->default_value = 600;

                values = (qahw_effect_param_t *)buf32;
                values->psize = sizeof(int32_t);
                values->vsize = sizeof(int32_t);
                *(int32_t *)values->data = BASSBOOST_PARAM_STRENGTH;
                memcpy((values->data + values->psize), &thr_ctxt->default_value, values->vsize);
                rc = qahw_effect_command(effect_handle, QAHW_EFFECT_CMD_SET_PARAM,
                                     array_size, (void *)values,
                                     thr_ctxt->reply_size, thr_ctxt->reply_data);
                if (rc != 0) {
                    fprintf(stderr, "effect_command() returns %d\n", rc);
                }else {
                     thr_ctxt->default_flag = false;
                }
            }
            break;
        case(EFFECT_PROC):
            //qahw_effect_process();
            break;
        case(EFFECT_RELEASE):
            rc = qahw_effect_release(lib_handle, effect_handle);
            if (rc != 0) {
                fprintf(stderr, "effect_release() returns %d\n", rc);
            }
            break;
        case(EFFECT_UNLOAD_LIB):
            rc = qahw_effect_unload_library(lib_handle);
            if (rc != 0) {
                fprintf(stderr, "effect_unload_library() returns %d\n", rc);
            }
            break;
        default:
            fprintf(stderr, "unrecognized command %d\n", thr_ctxt->cmd);
        }
    }
    pthread_mutex_unlock(&thr_ctxt->mutex);

    return NULL;
}

/* THREAD BODY OF VIRTUALIZER */
void *virtualizer_thread_func(void* data) {
    thread_data_t            *thr_ctxt = (thread_data_t *)data;
    qahw_effect_lib_handle_t lib_handle;
    qahw_effect_handle_t     effect_handle;
    qahw_effect_descriptor_t effect_desc;
    int32_t                  rc;
    int                      reply_data;
    uint32_t                 reply_size = sizeof(int);
    uint32_t array_size = sizeof(qahw_effect_param_t) + 2 * sizeof(int32_t);
    uint32_t      buf32[array_size];
    qahw_effect_param_t *values;

    pthread_mutex_lock(&thr_ctxt->mutex);
    while(!thr_ctxt->exit) {
        // suspend thread till signaled
        fprintf(stdout, "suspend effect thread\n");
        pthread_cond_wait(&thr_ctxt->loop_cond, &thr_ctxt->mutex);
        fprintf(stdout, "awake effect thread\n");

        switch(thr_ctxt->cmd) {
        case(EFFECT_LOAD_LIB):
            lib_handle = qahw_effect_load_library(QAHW_EFFECT_VIRTUALIZER_LIBRARY);
            break;
        case(EFFECT_GET_DESC):
            rc = qahw_effect_get_descriptor(lib_handle, SL_IID_VIRTUALIZER_UUID, &effect_desc);
            if (rc != 0) {
                fprintf(stderr, "effect_get_descriptor() returns %d\n", rc);
            }
            break;
        case(EFFECT_CREATE):
            rc = qahw_effect_create(lib_handle, SL_IID_VIRTUALIZER_UUID,
                                    thr_ctxt->io_handle, &effect_handle);
            if (rc != 0) {
                fprintf(stderr, "effect_create() returns %d\n", rc);
            }
            break;
        case(EFFECT_CMD):
            thr_ctxt->reply_size = (uint32_t *)&reply_size;
            thr_ctxt->reply_data = (void *)&reply_data;
            rc = qahw_effect_command(effect_handle, thr_ctxt->cmd_code,
                                     thr_ctxt->cmd_size, thr_ctxt->cmd_data,
                                     thr_ctxt->reply_size, thr_ctxt->reply_data);
            if (rc != 0) {
                fprintf(stderr, "effect_command() returns %d\n", rc);
            }
            if (thr_ctxt->default_flag && (thr_ctxt->cmd_code == QAHW_EFFECT_CMD_ENABLE)) {
                if (thr_ctxt->default_value == -1)
                    thr_ctxt->default_value = 600;

                values = (qahw_effect_param_t *)buf32;
                values->psize = sizeof(int32_t);
                values->vsize = sizeof(int32_t);
                *(int32_t *)values->data = VIRTUALIZER_PARAM_STRENGTH;
                memcpy((values->data + values->psize), &thr_ctxt->default_value, values->vsize);
                rc = qahw_effect_command(effect_handle, QAHW_EFFECT_CMD_SET_PARAM,
                                     array_size, (void *)values,
                                     thr_ctxt->reply_size, thr_ctxt->reply_data);
                if (rc != 0) {
                    fprintf(stderr, "effect_command() returns %d\n", rc);
                }else {
                     thr_ctxt->default_flag = false;
                }
            }
            break;
        case(EFFECT_PROC):
            //qahw_effect_process();
            break;
        case(EFFECT_RELEASE):
            rc = qahw_effect_release(lib_handle, effect_handle);
            if (rc != 0) {
                fprintf(stderr, "effect_release() returns %d\n", rc);
            }
            break;
        case(EFFECT_UNLOAD_LIB):
            rc = qahw_effect_unload_library(lib_handle);
            if (rc != 0) {
                fprintf(stderr, "effect_unload_library() returns %d\n", rc);
            }
            break;
        default:
            fprintf(stderr, "unrecognized command %d\n", thr_ctxt->cmd);
        }
    }
    pthread_mutex_unlock(&thr_ctxt->mutex);

    return NULL;
}

/* THREAD BODY OF EQUALIZER */
void *equalizer_thread_func(void* data) {
    thread_data_t            *thr_ctxt = (thread_data_t *)data;
    qahw_effect_lib_handle_t lib_handle;
    qahw_effect_handle_t     effect_handle;
    qahw_effect_descriptor_t effect_desc;
    int32_t                  rc;
    int                      reply_data;
    uint32_t                 reply_size = sizeof(int);
    uint32_t array_size = sizeof(qahw_effect_param_t) + 2 * sizeof(int32_t);
    uint32_t      buf32[array_size];
    qahw_effect_param_t *values;

    pthread_mutex_lock(&thr_ctxt->mutex);
    while(!thr_ctxt->exit) {
        // suspend thread till signaled
        fprintf(stdout, "suspend effect thread\n");
        pthread_cond_wait(&thr_ctxt->loop_cond, &thr_ctxt->mutex);
        fprintf(stdout, "awake effect thread\n");

        switch(thr_ctxt->cmd) {
        case(EFFECT_LOAD_LIB):
            lib_handle = qahw_effect_load_library(QAHW_EFFECT_EQUALIZER_LIBRARY);
            break;
        case(EFFECT_GET_DESC):
            rc = qahw_effect_get_descriptor(lib_handle, SL_IID_EQUALIZER_UUID, &effect_desc);
            if (rc != 0) {
                fprintf(stderr, "effect_get_descriptor() returns %d\n", rc);
            }
            break;
        case(EFFECT_CREATE):
            rc = qahw_effect_create(lib_handle, SL_IID_EQUALIZER_UUID,
                                    thr_ctxt->io_handle, &effect_handle);
            if (rc != 0) {
                fprintf(stderr, "effect_create() returns %d\n", rc);
            }
            break;
        case(EFFECT_CMD):
            thr_ctxt->reply_size = (uint32_t *)&reply_size;
            thr_ctxt->reply_data = (void *)&reply_data;
            rc = qahw_effect_command(effect_handle, thr_ctxt->cmd_code,
                                     thr_ctxt->cmd_size, thr_ctxt->cmd_data,
                                     thr_ctxt->reply_size, thr_ctxt->reply_data);
            if (rc != 0) {
                fprintf(stderr, "effect_command() returns %d\n", rc);
            }
            if (thr_ctxt->default_flag && (thr_ctxt->cmd_code == QAHW_EFFECT_CMD_ENABLE)) {
                if (thr_ctxt->default_value == -1)
                    thr_ctxt->default_value = 2;

                values = (qahw_effect_param_t *)buf32;
                values->psize = sizeof(int32_t);
                values->vsize = sizeof(int32_t);
                *(int32_t *)values->data = EQ_PARAM_CUR_PRESET;
                memcpy((values->data + values->psize), &thr_ctxt->default_value, values->vsize);
                rc = qahw_effect_command(effect_handle, QAHW_EFFECT_CMD_SET_PARAM,
                                     array_size, (void *)values,
                                     thr_ctxt->reply_size, thr_ctxt->reply_data);
                if (rc != 0) {
                    fprintf(stderr, "effect_command() returns %d\n", rc);
                }else {
                     thr_ctxt->default_flag = false;
                }
            }
            break;
        case(EFFECT_PROC):
            //qahw_effect_process();
            break;
        case(EFFECT_RELEASE):
            rc = qahw_effect_release(lib_handle, effect_handle);
            if (rc != 0) {
                fprintf(stderr, "effect_release() returns %d\n", rc);
            }
            break;
        case(EFFECT_UNLOAD_LIB):
            rc = qahw_effect_unload_library(lib_handle);
            if (rc != 0) {
                fprintf(stderr, "effect_unload_library() returns %d\n", rc);
            }
            break;
        default:
            fprintf(stderr, "unrecognized command %d\n", thr_ctxt->cmd);
        }
    }
    pthread_mutex_unlock(&thr_ctxt->mutex);

    return NULL;
}

/* THREAD BODY OF VISUALIZER */
void *visualizer_thread_func(void* data __unused) {
    /* TODO */
    return NULL;
}

/* THREAD BODY OF REVERB */
void *reverb_thread_func(void* data) {
    thread_data_t            *thr_ctxt = (thread_data_t *)data;
    qahw_effect_lib_handle_t lib_handle;
    qahw_effect_handle_t     effect_handle;
    qahw_effect_descriptor_t effect_desc;
    int32_t                  rc;
    int                      reply_data;
    uint32_t                 reply_size = sizeof(int);
    uint32_t array_size = sizeof(qahw_effect_param_t) + 2 * sizeof(int32_t);
    uint32_t      buf32[array_size];
    qahw_effect_param_t *values;

    pthread_mutex_lock(&thr_ctxt->mutex);
    while(!thr_ctxt->exit) {
        // suspend thread till signaled
        fprintf(stdout, "suspend effect thread\n");
        pthread_cond_wait(&thr_ctxt->loop_cond, &thr_ctxt->mutex);
        fprintf(stdout, "awake effect thread\n");

        switch(thr_ctxt->cmd) {
        case(EFFECT_LOAD_LIB):
            lib_handle = qahw_effect_load_library(QAHW_EFFECT_PRESET_REVERB_LIBRARY);
            break;
        case(EFFECT_GET_DESC):
            rc = qahw_effect_get_descriptor(lib_handle, SL_IID_INS_PRESETREVERB_UUID, &effect_desc);
            if (rc != 0) {
                fprintf(stderr, "effect_get_descriptor() returns %d\n", rc);
            }
            break;
        case(EFFECT_CREATE):
            rc = qahw_effect_create(lib_handle, SL_IID_INS_PRESETREVERB_UUID,
                                    thr_ctxt->io_handle, &effect_handle);
            if (rc != 0) {
                fprintf(stderr, "effect_create() returns %d\n", rc);
            }
            break;
        case(EFFECT_CMD):
            thr_ctxt->reply_size = (uint32_t *)&reply_size;
            thr_ctxt->reply_data = (void *)&reply_data;
            rc = qahw_effect_command(effect_handle, thr_ctxt->cmd_code,
                                     thr_ctxt->cmd_size, thr_ctxt->cmd_data,
                                     thr_ctxt->reply_size, thr_ctxt->reply_data);
            if (rc != 0) {
                fprintf(stderr, "effect_command() returns %d\n", rc);
            }
            if (thr_ctxt->default_flag && (thr_ctxt->cmd_code == QAHW_EFFECT_CMD_ENABLE)) {
                if (thr_ctxt->default_value == -1)
                    thr_ctxt->default_value = 2;

                values = (qahw_effect_param_t *)buf32;
                values->psize = sizeof(int32_t);
                values->vsize = sizeof(int32_t);
                *(int32_t *)values->data = REVERB_PARAM_PRESET;
                memcpy((values->data + values->psize), &thr_ctxt->default_value, values->vsize);
                rc = qahw_effect_command(effect_handle, QAHW_EFFECT_CMD_SET_PARAM,
                                     array_size, (void *)values,
                                     thr_ctxt->reply_size, thr_ctxt->reply_data);
                if (rc != 0) {
                    fprintf(stderr, "effect_command() returns %d\n", rc);
                }else {
                     thr_ctxt->default_flag = false;
                }
            }
            break;
        case(EFFECT_PROC):
            //qahw_effect_process();
            break;
        case(EFFECT_RELEASE):
            rc = qahw_effect_release(lib_handle, effect_handle);
            if (rc != 0) {
                fprintf(stderr, "effect_release() returns %d\n", rc);
            }
            break;
        case(EFFECT_UNLOAD_LIB):
            rc = qahw_effect_unload_library(lib_handle);
            if (rc != 0) {
                fprintf(stderr, "effect_unload_library() returns %d\n", rc);
            }
            break;
        default:
            fprintf(stderr, "unrecognized command %d\n", thr_ctxt->cmd);
        }
    }
    pthread_mutex_unlock(&thr_ctxt->mutex);

    return NULL;
}

void *command_thread_func(void* data) {
    cmd_data_t    *thr_ctxt = (cmd_data_t *)data;
    thread_data_t *fx_ctxt = *(thr_ctxt->fx_data_ptr);
    char          cmd_str[MAX_CMD_STR_SIZE];
    int           cmd_key;
    uint32_t      size = sizeof(qahw_effect_param_t) + 2 * sizeof(int32_t);
    uint32_t      size_2 = sizeof(qahw_effect_param_t) + 3 * sizeof(int32_t);
    uint32_t      buf32[size];
    uint32_t      buf32_2[size_2];
    int           strength;
    uint32_t      preset;
    int           level;
    uint16_t      band_idx;
    qahw_effect_param_t *param = (qahw_effect_param_t *)buf32;
    qahw_effect_param_t *param_2 = (qahw_effect_param_t *)buf32_2;

    /* Register the SIGUSR1 to close this thread properly
       as it is waiting for input in while loop */
    if (signal(SIGUSR1, stop_effect_command_thread_handler) == SIG_ERR) {
        fprintf(stderr, "Failed to register SIGUSR1:%d\n",errno);
    }

    while(!thr_ctxt->exit) {
        if (fgets(cmd_str, sizeof(cmd_str), stdin) == NULL) {
            fprintf(stderr, "read error\n");
            break;
        }
        strtok(cmd_str, "\n");

        // no ops if there's no effect thread running
        // or input string is invalid
        if (!is_valid_input(cmd_str) || !fx_ctxt)
            continue;

        cmd_key = get_key_from_name(fx_ctxt->who_am_i, cmd_str);
        switch (cmd_key) {
        case TTY_ENABLE:
            notify_effect_command(fx_ctxt, EFFECT_CMD, QAHW_EFFECT_CMD_ENABLE, 0, NULL);
            break;
        case TTY_DISABLE:
            notify_effect_command(fx_ctxt, EFFECT_CMD, QAHW_EFFECT_CMD_DISABLE, 0, NULL);
            break;
        case TTY_BB_SET_STRENGTH:
        case TTY_VT_SET_STRENGTH:
        case TTY_ASPHERE_SET_STRENGTH:
            {
                fprintf(stdout, "%s", get_prompt_from_name(fx_ctxt->who_am_i, cmd_str));
                if (fgets(cmd_str, sizeof(cmd_str), stdin) == NULL) {
                    fprintf(stderr, "unrecognized strength number!\n");
                    break;
                }

                strtok(cmd_str, "\n");
                strength = atoi(cmd_str);
                if ((strength < 0) || (strength > 1000)) {
                    fprintf(stderr, "invalid strength number!\n");
                    break;
                }

                param->psize = sizeof(int32_t);
                *(int32_t *)param->data = ((cmd_key == TTY_BB_SET_STRENGTH) ? BASSBOOST_PARAM_STRENGTH :
                                           ((cmd_key == TTY_VT_SET_STRENGTH) ? VIRTUALIZER_PARAM_STRENGTH:
                                           ASPHERE_PARAM_STRENGTH));
                param->vsize = sizeof(int32_t);
                memcpy((param->data + param->psize), &strength, param->vsize);

                notify_effect_command(fx_ctxt, EFFECT_CMD, QAHW_EFFECT_CMD_SET_PARAM, size, param);
                break;
            }
        case TTY_EQ_SET_PRESET:
            {
                fprintf(stdout, "%s", get_prompt_from_name(fx_ctxt->who_am_i, cmd_str));
                if (fgets(cmd_str, sizeof(cmd_str), stdin) == NULL) {
                    fprintf(stderr, "unrecognized preset!\n");
                    break;
                }

                strtok(cmd_str, "\n");
                preset = atoi(cmd_str);
                if ((preset < EQ_PRESET_NORMAL) || (preset > EQ_PRESET_LAST)) {
                    fprintf(stderr, "invalid preset!\n");
                    break;
                }

                param->psize = sizeof(int32_t);
                *(int32_t *)param->data = EQ_PARAM_CUR_PRESET;
                param->vsize = sizeof(int32_t);
                memcpy((param->data + param->psize), &preset, param->vsize);

                notify_effect_command(fx_ctxt, EFFECT_CMD, QAHW_EFFECT_CMD_SET_PARAM, size, param);
                break;
            }
        case TTY_EQ_SET_CUSTOM:
            {
                fprintf(stdout, "%s", get_prompt_from_name(fx_ctxt->who_am_i, cmd_str));
                for (band_idx = 0; band_idx < NUM_EQ_BANDS; ++band_idx) {
                    fprintf(stdout, "input level for band (%d - %dHz) (range from -15 to +15):\n",
                            band_idx, qahw_equalizer_band_freqs[band_idx]);
                    if (fgets(cmd_str, sizeof(cmd_str), stdin) == NULL) {
                        fprintf(stderr, "unrecognized band level!\n");
                        break;
                    }

                    strtok(cmd_str, "\n");
                    level = atoi(cmd_str) * 100;
                    if ((level < -1500) || (level > 1500)) {
                        fprintf(stderr, "equalizer band level out of range!\n");
                        break;
                    }

                    param_2->psize = 2 * sizeof(int32_t);
                    *(int32_t *)param_2->data = EQ_PARAM_BAND_LEVEL;
                    *((int32_t *)param_2->data + 1) = band_idx;
                    param_2->vsize = sizeof(int32_t);
                    memcpy((param_2->data + param_2->psize), &level, param_2->vsize);

                    notify_effect_command(fx_ctxt, EFFECT_CMD, QAHW_EFFECT_CMD_SET_PARAM, size, param_2);
                }
                break;
            }
            break;
        case TTY_RB_SET_PRESET:
            {
                fprintf(stdout, "%s", get_prompt_from_name(fx_ctxt->who_am_i, cmd_str));
                if (fgets(cmd_str, sizeof(cmd_str), stdin) == NULL) {
                    fprintf(stderr, "unrecognized preset!\n");
                    break;
                }

                strtok(cmd_str, "\n");
                preset = atoi(cmd_str);
                if ((preset < REVERB_PRESET_NONE) || (preset > REVERB_PRESET_LAST)) {
                    fprintf(stderr, "invalid preset!\n");
                    break;
                }

                param->psize = sizeof(int32_t);
                *(int32_t *)param->data = REVERB_PARAM_PRESET;
                param->vsize = sizeof(int32_t);
                memcpy((param->data + param->psize), &preset, param->vsize);

                notify_effect_command(fx_ctxt, EFFECT_CMD, QAHW_EFFECT_CMD_SET_PARAM, size, param);
                break;
            }
        default:
            fprintf(stderr, "unknown command %d\n", cmd_key);
            break;
        }
    }

    return NULL;
}

thread_data_t *create_effect_thread(int effect_idx, thread_func_t func_ptr) {
    int result;

    thread_data_t *ethread_data = (thread_data_t *)calloc(1, sizeof(thread_data_t));
    ethread_data->exit = false;
    ethread_data->who_am_i = effect_idx;

    pthread_attr_init(&ethread_data->attr);
    pthread_attr_setdetachstate(&ethread_data->attr, PTHREAD_CREATE_JOINABLE);
    pthread_mutex_init(&ethread_data->mutex, NULL);
    if (pthread_cond_init(&ethread_data->loop_cond, NULL) != 0) {
        fprintf(stderr, "pthread_cond_init fails\n");
        return NULL;
    }
    // create effect thread
    result = pthread_create(&ethread_data->effect_thread, &ethread_data->attr,
                            func_ptr, ethread_data);

    if (result < 0) {
        fprintf(stderr, "Could not create effect thread!\n");
        return NULL;
    }

    return ethread_data;
}

void notify_effect_command(thread_data_t *ethread_data,
                           int cmd, uint32_t cmd_code,
                           uint32_t cmd_size, void *cmd_data) {
    if (ethread_data == NULL) {
        fprintf(stderr, "invalid thread data\n");
        return;
    }

    // leave interval to let thread consume the previous cond signal
    usleep(500000);

    pthread_mutex_lock(&ethread_data->mutex);
    ethread_data->cmd = cmd;
    ethread_data->cmd_code = cmd_code;
    ethread_data->cmd_size = cmd_size;
    ethread_data->cmd_data = cmd_data;
    pthread_mutex_unlock(&ethread_data->mutex);
    pthread_cond_signal(&ethread_data->loop_cond);

    return;
}

void destroy_effect_thread(thread_data_t *ethread_data) {
    int result;

    if (ethread_data == NULL) {
        fprintf(stderr, "invalid thread data\n");
        return;
    }

    pthread_mutex_lock(&ethread_data->mutex);
    ethread_data->exit = true;
    pthread_mutex_unlock(&ethread_data->mutex);
    pthread_cond_signal(&ethread_data->loop_cond);

    result = pthread_join(ethread_data->effect_thread, NULL);
    if (result < 0) {
        fprintf(stderr, "Fail to join effect thread!\n");
        return;
    }
    pthread_mutex_destroy(&ethread_data->mutex);
    pthread_cond_destroy(&ethread_data->loop_cond);

    return;
}

int get_key_from_name(int fx_id, const char *name) {
    cmd_def_t *tmp = cmds_lookup_table[fx_id];
    int       rc   = -EINVAL;
    int       i;

    if (name == NULL)
        goto done;

    for(i = 0; i < TTY_CMD_MAX; i++) {
        if (strcmp(tmp[i].cmd_str, name) == 0) {
            rc = tmp[i].cmd_id;
            break;
        }
    }

done:
    return rc;
}

char *get_prompt_from_name(int fx_id, const char *name) {
    cmd_def_t *tmp = cmds_lookup_table[fx_id];
    char       *rc = NULL;
    int       i;

    if (name == NULL)
        goto done;

    for(i = 0; i < TTY_CMD_MAX; i++) {
        if (strcmp(tmp[i].cmd_str, name) == 0) {
            rc = tmp[i].cmd_prompt;
            break;
        }
    }

done:
    return rc;
}

bool is_valid_input(char *inputs) {
    char *input_ptr = inputs;

    if (input_ptr == NULL)
        return false;

    while (*input_ptr == ' ')
        input_ptr++;

    if ((*input_ptr != '\0') && (*input_ptr != '\n'))
        return true;

    return false;
}

/* THREAD BODY OF AUDIOSPHERE */
void *asphere_thread_func(void* data) {
    thread_data_t            *thr_ctxt = (thread_data_t *)data;
    qahw_effect_lib_handle_t lib_handle;
    qahw_effect_handle_t     effect_handle;
    qahw_effect_descriptor_t effect_desc;
    int32_t                  rc;
    int                      reply_data;
    uint32_t                 reply_size = sizeof(int);
    uint32_t array_size = sizeof(qahw_effect_param_t) + 2 * sizeof(int32_t);
    uint32_t      buf32[array_size], buf32_2[array_size];
    qahw_effect_param_t *values;
    int enable;

    pthread_mutex_lock(&thr_ctxt->mutex);
    while(!thr_ctxt->exit) {
        // suspend thread till signaled
        fprintf(stdout, "suspend effect thread\n");
        pthread_cond_wait(&thr_ctxt->loop_cond, &thr_ctxt->mutex);
        fprintf(stdout, "awake effect thread\n");

        switch(thr_ctxt->cmd) {
        case(EFFECT_LOAD_LIB):
            lib_handle = qahw_effect_load_library(QAHW_EFFECT_AUDIOSPHERE_LIBRARY);
            break;
        case(EFFECT_GET_DESC):
            rc = qahw_effect_get_descriptor(lib_handle, SL_IID_AUDIOSPHERE_UUID, &effect_desc);
            if (rc != 0) {
                fprintf(stderr, "effect_get_descriptor() returns %d\n", rc);
            }
            break;
        case(EFFECT_CREATE):
            rc = qahw_effect_create(lib_handle, SL_IID_AUDIOSPHERE_UUID,
                                    thr_ctxt->io_handle, &effect_handle);
            if (rc != 0) {
                fprintf(stderr, "effect_create() returns %d\n", rc);
            }
            break;
        case(EFFECT_CMD):
            thr_ctxt->reply_size = (uint32_t *)&reply_size;
            thr_ctxt->reply_data = (void *)&reply_data;
            rc = qahw_effect_command(effect_handle, thr_ctxt->cmd_code,
                                     thr_ctxt->cmd_size, thr_ctxt->cmd_data,
                                     thr_ctxt->reply_size, thr_ctxt->reply_data);
            if (rc != 0) {
                fprintf(stderr, "effect_command() returns %d\n", rc);
            }
            if (thr_ctxt->cmd_code == QAHW_EFFECT_CMD_ENABLE || thr_ctxt->cmd_code == QAHW_EFFECT_CMD_DISABLE) {
                enable = ((thr_ctxt->cmd_code == QAHW_EFFECT_CMD_ENABLE) ? 1 : 0);

                values = (qahw_effect_param_t *)buf32_2;
                values->psize = 2 * sizeof(int32_t);
                values->vsize = sizeof(int32_t);
                *(int32_t *)values->data = ASPHERE_PARAM_ENABLE;
                memcpy((values->data + values->psize), &enable, values->vsize);
                rc = qahw_effect_command(effect_handle, QAHW_EFFECT_CMD_SET_PARAM,
                                     array_size, (void *)values,
                                     thr_ctxt->reply_size, thr_ctxt->reply_data);
                if (rc != 0) {
                    fprintf(stderr, "effect_command() returns %d\n", rc);
                }
            }
            if (thr_ctxt->default_flag && (thr_ctxt->cmd_code == QAHW_EFFECT_CMD_ENABLE)) {
                if (thr_ctxt->default_value == -1)
                    thr_ctxt->default_value = 600;

                values = (qahw_effect_param_t *)buf32;
                values->psize = sizeof(int32_t);
                values->vsize = sizeof(int32_t);
                *(int32_t *)values->data = ASPHERE_PARAM_STRENGTH;
                memcpy((values->data + values->psize), &thr_ctxt->default_value, values->vsize);
                rc = qahw_effect_command(effect_handle, QAHW_EFFECT_CMD_SET_PARAM,
                                     array_size, (void *)values,
                                     thr_ctxt->reply_size, thr_ctxt->reply_data);
                if (rc != 0) {
                    fprintf(stderr, "effect_command() returns %d\n", rc);
                }else {
                     thr_ctxt->default_flag = false;
                }
            }
            break;
        case(EFFECT_PROC):
            //qahw_effect_process();
            break;
        case(EFFECT_RELEASE):
            rc = qahw_effect_release(lib_handle, effect_handle);
            if (rc != 0) {
                fprintf(stderr, "effect_release() returns %d\n", rc);
            }
            break;
        case(EFFECT_UNLOAD_LIB):
            rc = qahw_effect_unload_library(lib_handle);
            if (rc != 0) {
                fprintf(stderr, "effect_unload_library() returns %d\n", rc);
            }
            break;
        default:
            fprintf(stderr, "unrecognized command %d\n", thr_ctxt->cmd);
        }
    }
    pthread_mutex_unlock(&thr_ctxt->mutex);

    return NULL;
}
