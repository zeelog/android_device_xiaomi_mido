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

#define LOG_TAG "qahw_effect"
//#define LOG_NDEBUG 0
#define LOG_NDDEBUG 0

#include <cutils/list.h>
#include <dlfcn.h>
#include <utils/Log.h>
#include <hardware/audio.h>
#include <hardware/audio_effect.h>
#include <stdlib.h>
#include <pthread.h>
#include "qahw.h"

// The current effect API version.
#define QAHW_EFFECT_API_VERSION_CURRENT QAHW_EFFECT_API_VERSION_0_0
#define PATH_MAX 4096

typedef struct {
    char                   lib_path[PATH_MAX];
    pthread_mutex_t        lock;
    uint32_t               ref_count;
    audio_effect_library_t *desc;
    void                   *handle;
    struct listnode        lib_list;
} qahw_effect_lib_t;

// list of loaded effect libraries
static struct listnode effect_libraries_list;
static int effect_libraries_count = 0;
static pthread_mutex_t effect_libraries_lock = PTHREAD_MUTEX_INITIALIZER;

qahw_effect_lib_t *get_qahw_effect_lib_by_name(const char *lib_path) {
    struct listnode *node = NULL;
    qahw_effect_lib_t *effect_lib = NULL, *effect_lib_temp = NULL;

    if (lib_path == NULL)
        goto exit;

    list_for_each(node, &effect_libraries_list) {
        effect_lib_temp = node_to_item(node, qahw_effect_lib_t, lib_list);
        if(!strncmp(lib_path, effect_lib_temp->lib_path, PATH_MAX)) {
            effect_lib = effect_lib_temp;
            break;
        }
    }
exit:
    return effect_lib;
}


qahw_effect_lib_t *get_qahw_effect_lib_by_desc(qahw_effect_lib_handle_t handle) {
    struct listnode *node = NULL;
    qahw_effect_lib_t *effect_lib = NULL, *effect_lib_temp = NULL;

    if (handle == NULL)
        goto exit;

    list_for_each(node, &effect_libraries_list) {
        effect_lib_temp = node_to_item(node, qahw_effect_lib_t, lib_list);
        if (effect_lib_temp->desc == (audio_effect_library_t *)handle) {
            effect_lib = effect_lib_temp;
            break;
        }
    }
exit:
    return effect_lib;
}


qahw_effect_lib_handle_t qahw_effect_load_library_l(const char *lib_path) {
    audio_effect_library_t *desc;
    qahw_effect_lib_t      *qahw_effect_lib;
    void                   *handle;

    if (strlen(lib_path) >= PATH_MAX -1) {
        ALOGE("%s: effect libraries path too long", __func__);
        return NULL;
    }

    /* return existing lib handler if already loaded */
    pthread_mutex_lock(&effect_libraries_lock);
    if (effect_libraries_count > 0) {
        qahw_effect_lib = get_qahw_effect_lib_by_name(lib_path);
        if (qahw_effect_lib != NULL) {
            desc = qahw_effect_lib->desc;
            pthread_mutex_lock(&qahw_effect_lib->lock);
            qahw_effect_lib->ref_count++;
            pthread_mutex_unlock(&qahw_effect_lib->lock);
            goto done;
        }
    }

    handle = dlopen(lib_path, RTLD_NOW);
    if (handle == NULL) {
        ALOGE("%s: failed to dlopen lib %s", __func__, lib_path);
        goto error;
    }

    desc = (audio_effect_library_t *)dlsym(handle, AUDIO_EFFECT_LIBRARY_INFO_SYM_AS_STR);
    if (desc == NULL) {
        ALOGE("%s: could not find symbol %s", __func__, AUDIO_EFFECT_LIBRARY_INFO_SYM_AS_STR);
        goto error;
    }

    if (AUDIO_EFFECT_LIBRARY_TAG != desc->tag) {
        ALOGE("%s: bad tag %08x in lib info struct", __func__, desc->tag);
        goto error;
    }

    if (EFFECT_API_VERSION_MAJOR(desc->version) !=
            EFFECT_API_VERSION_MAJOR(EFFECT_LIBRARY_API_VERSION)) {
        ALOGE("%s: bad lib version %08x", __func__, desc->version);
        goto error;
    }

    qahw_effect_lib = (qahw_effect_lib_t *)calloc(1, sizeof(qahw_effect_lib_t));
    if (qahw_effect_lib == NULL) {
        ALOGE("%s: calloc failed", __func__);
        goto error;
    }

    if (!effect_libraries_count)
        list_init(&effect_libraries_list);
    effect_libraries_count++;

    /* init and load effect lib into global list */
    strlcpy(qahw_effect_lib->lib_path, lib_path, PATH_MAX);
    pthread_mutex_init(&qahw_effect_lib->lock, (const pthread_mutexattr_t *) NULL);
    qahw_effect_lib->ref_count = 1;
    qahw_effect_lib->desc = desc;
    qahw_effect_lib->handle = handle;

    list_add_tail(&effect_libraries_list, &qahw_effect_lib->lib_list);

done:
    pthread_mutex_unlock(&effect_libraries_lock);
    return (qahw_effect_lib_handle_t)desc;

error:
    if (handle != NULL)
        dlclose(handle);

    pthread_mutex_unlock(&effect_libraries_lock);
    return NULL;
}


int32_t qahw_effect_unload_library_l(qahw_effect_lib_handle_t handle) {
    qahw_effect_lib_t *qahw_effect_lib;

    pthread_mutex_lock(&effect_libraries_lock);
    if (effect_libraries_count <= 0) {
        ALOGW("%s: no valid libraries loaded", __func__);
        pthread_mutex_unlock(&effect_libraries_lock);
        return -EINVAL;
    }

    qahw_effect_lib = get_qahw_effect_lib_by_desc(handle);
    if (qahw_effect_lib == NULL) {
        ALOGW("%s: effect lib handle(%p) not in loaded queue", __func__, (void *)handle);
        pthread_mutex_unlock(&effect_libraries_lock);
        return -EINVAL;
    }

    pthread_mutex_lock(&qahw_effect_lib->lock);
    qahw_effect_lib->ref_count--;
    if (qahw_effect_lib->ref_count > 0) {
        ALOGW("%s: skip unloading effect lib, ref count %d", __func__, qahw_effect_lib->ref_count);
        pthread_mutex_unlock(&qahw_effect_lib->lock);
        goto done;
    }

    if (qahw_effect_lib->handle)
        dlclose(qahw_effect_lib->handle);
    effect_libraries_count--;
    list_remove(&qahw_effect_lib->lib_list);
    pthread_mutex_unlock(&qahw_effect_lib->lock);
    pthread_mutex_destroy(&qahw_effect_lib->lock);
    free(qahw_effect_lib);

done:
    pthread_mutex_unlock(&effect_libraries_lock);
    return 0;
}


int32_t qahw_effect_create_l(qahw_effect_lib_handle_t handle,
                           const qahw_effect_uuid_t *uuid,
                           int32_t io_handle,
                           qahw_effect_handle_t *effect_handle) {
    int32_t rc = -EINVAL;
    audio_effect_library_t *desc = (audio_effect_library_t *)handle;

    if (desc != NULL) {
        rc = desc->create_effect((const effect_uuid_t *)uuid, 0, io_handle,
                (effect_handle_t *)effect_handle);
    }

    return rc;
}


int32_t qahw_effect_release_l(qahw_effect_lib_handle_t handle,
                            qahw_effect_handle_t effect_handle) {
    int32_t rc = -EINVAL;
    audio_effect_library_t *desc = (audio_effect_library_t *)handle;

    if (desc != NULL) {
        rc = desc->release_effect((effect_handle_t)effect_handle);
    }

    return rc;
}


int32_t qahw_effect_get_descriptor_l(qahw_effect_lib_handle_t handle,
                                   const qahw_effect_uuid_t *uuid,
                                   qahw_effect_descriptor_t *effect_desc) {
    int32_t rc = -EINVAL;
    audio_effect_library_t *desc = (audio_effect_library_t *)handle;

    if (desc != NULL) {
        rc = desc->get_descriptor((const effect_uuid_t *)uuid, (effect_descriptor_t *)effect_desc);
    }

    return rc;
}


int32_t qahw_effect_get_version_l() {
    return QAHW_EFFECT_API_VERSION_CURRENT;
}


int32_t qahw_effect_process_l(qahw_effect_handle_t self,
                            qahw_audio_buffer_t *in_buffer,
                            qahw_audio_buffer_t *out_buffer) {
    int32_t rc = -EINVAL;
    struct effect_interface_s *itfe;

    if (self) {
        itfe = *((struct effect_interface_s **)self);
        if (itfe) {
            rc = itfe->process((effect_handle_t)self,
                               (audio_buffer_t *)in_buffer,
                               (audio_buffer_t *)out_buffer);
        }
    }

    return rc;
}


int32_t qahw_effect_command_l(qahw_effect_handle_t self,
                            uint32_t cmd_code,
                            uint32_t cmd_size,
                            void *cmd_data,
                            uint32_t *reply_size,
                            void *reply_data) {
    int32_t rc = -EINVAL;
    struct effect_interface_s *itfe;

    if (self) {
        itfe = *((struct effect_interface_s **)self);
        if (itfe) {
            rc = itfe->command((effect_handle_t)self, cmd_code, cmd_size,
                               cmd_data, reply_size, reply_data);
        }
    }

    return rc;
}


int32_t qahw_effect_process_reverse_l(qahw_effect_handle_t self,
                                    qahw_audio_buffer_t *in_buffer,
                                    qahw_audio_buffer_t *out_buffer) {
    int32_t rc = -EINVAL;
    struct effect_interface_s *itfe;

    if (self) {
        itfe = *((struct effect_interface_s **)self);
        if (itfe) {
            rc = itfe->process_reverse((effect_handle_t)self,
                                       (audio_buffer_t *)in_buffer,
                                       (audio_buffer_t *)out_buffer);
        }
    }

    return rc;
}

