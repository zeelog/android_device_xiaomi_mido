/*--------------------------------------------------------------------------
Copyright (c) 2017, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
--------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <string.h>
#include <dlfcn.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>

#include "hypv_intercept.h"
#include "vidc_debug.h"

typedef HVFE_HANDLE (*video_fe_open_func)(const char*, int, hvfe_callback_t*);
typedef int (*video_fe_ioctl_func)(HVFE_HANDLE, int, void*);
typedef int (*video_fe_close_func)(HVFE_HANDLE);

#define MAX_HVFE_HANDLE_STORAGE     16
#define HYPV_HANDLE_SIGNATURE       0x2bcd0000
#define HYPV_HANDLE_SIGNATURE_MASK  0xffff0000
#define HYPV_HANDLE_MASK            0x0000ffff

static void *hvfe_lib_handle = NULL;
static video_fe_open_func video_fe_open = NULL;
static video_fe_ioctl_func video_fe_ioctl = NULL;
static video_fe_close_func video_fe_close = NULL;
static pthread_mutex_t g_hvfe_handle_lock = PTHREAD_MUTEX_INITIALIZER;
static HVFE_HANDLE g_hvfe_handle_storage[MAX_HVFE_HANDLE_STORAGE];
static int g_hvfe_handle_count  = 0;

#define IS_HYPERVISOR_VIDEO_HANDLE(fd) ((fd & HYPV_HANDLE_SIGNATURE_MASK)==HYPV_HANDLE_SIGNATURE)
#define HYP_INITIALIZED  (g_hvfe_handle_count > 0)

static int add_to_hvfe_handle_storage(HVFE_HANDLE handle_to_store)
{
    int rc = 0;

    if (g_hvfe_handle_count >= MAX_HVFE_HANDLE_STORAGE) {
        DEBUG_PRINT_ERROR("reached max handle count");
        rc = -1;
    } else {
        int i;

        for (i = 0; i < MAX_HVFE_HANDLE_STORAGE; i++) {
            if (g_hvfe_handle_storage[i] == 0) {
                g_hvfe_handle_storage[i] = handle_to_store;
                rc = i;
                g_hvfe_handle_count++;
                break;
            }
        }
        if (i >= MAX_HVFE_HANDLE_STORAGE) {
            DEBUG_PRINT_ERROR("failed to find empty slot");
            rc = -1;
        }
    }

    return rc;
}

static int hypv_init(void)
{
    int rc = 0;

    hvfe_lib_handle = dlopen("libhyp_video_fe.so", RTLD_NOW);
    if (hvfe_lib_handle == NULL) {
        DEBUG_PRINT_ERROR("failed to open libhyp_video_fe");
        rc = -1;
    } else {
        video_fe_open = (video_fe_open_func)dlsym(hvfe_lib_handle, "video_fe_open");
        if (video_fe_open == NULL) {
            DEBUG_PRINT_ERROR("failed to get video_fe_open handle");
            rc = -1;
        } else {
            video_fe_ioctl = (video_fe_ioctl_func)dlsym(hvfe_lib_handle, "video_fe_ioctl");
            if (video_fe_ioctl == NULL) {
                DEBUG_PRINT_ERROR("failed to get video_fe_ioctl handle");
                rc = -1;
            } else {
                video_fe_close = (video_fe_close_func)dlsym(hvfe_lib_handle, "video_fe_close");
                if (video_fe_close == 0) {
                    DEBUG_PRINT_ERROR("failed to get video_fe_close handle");
                    rc = -1;
                }//video_fe_close
            } //video_fe_ioctl
        } //video_fe_open
    } //hvfe_lib_handle

    if (rc < 0 && hvfe_lib_handle) {
        dlclose(hvfe_lib_handle);
        hvfe_lib_handle = NULL;
    }

    return rc;
}

static void hypv_deinit(void)
{
    dlclose(hvfe_lib_handle);
    hvfe_lib_handle = NULL;

    return;
}

int hypv_open(const char *str, int flag, hvfe_callback_t* cb)
{
    int rc = 0;

    pthread_mutex_lock(&g_hvfe_handle_lock);
    if (!HYP_INITIALIZED) {
        if ((rc = hypv_init()) < 0) {
            DEBUG_PRINT_ERROR("hypervisor init failed");
            pthread_mutex_unlock(&g_hvfe_handle_lock);
            return rc;
        }
    }

    HVFE_HANDLE hvfe_handle = video_fe_open(str, flag, cb);
    DEBUG_PRINT_INFO("video_fe_open handle=%p", hvfe_handle);
    if (hvfe_handle == NULL) {
        DEBUG_PRINT_ERROR("video_fe_open failed");
        rc = -1;
    } else {
        int fd = add_to_hvfe_handle_storage(hvfe_handle);
        if (fd < 0) {
            DEBUG_PRINT_ERROR("failed to store hvfe handle");
            video_fe_close(hvfe_handle);
            rc = -1;
        } else {
            rc = (HYPV_HANDLE_SIGNATURE | fd);
        }
    }
    pthread_mutex_unlock(&g_hvfe_handle_lock);

    if (rc < 0)
        hypv_deinit();

    return rc;
}

int hypv_ioctl(int fd, int cmd, void *data)
{
    int rc = 0;

    if (!HYP_INITIALIZED) {
        DEBUG_PRINT_ERROR("hypervisor not initialized");
        return -1;
    }

    if (IS_HYPERVISOR_VIDEO_HANDLE(fd)) {
        int fd_index = fd & HYPV_HANDLE_MASK;
        if (fd_index >= MAX_HVFE_HANDLE_STORAGE) {
            DEBUG_PRINT_ERROR("invalid fd_index=%d", fd_index);
            rc = -1;
        } else {
            rc = video_fe_ioctl(g_hvfe_handle_storage[fd_index], cmd, data);
            DEBUG_PRINT_INFO("hyp ioctl: fd=%d, fd_index=%d, cmd=0x%x, data=0x%p, rc =%d",
                              fd, fd_index, cmd, data, rc);
        }
    } else {
        DEBUG_PRINT_ERROR("native ioctl: fd=%d, cmd=0x%x, data=0x%p", fd, cmd, data);
        rc = ioctl(fd, cmd, data);
    }

    return rc;
}

int hypv_close(int fd)
{
    int rc = 0;

    if (!HYP_INITIALIZED) {
        DEBUG_PRINT_ERROR("hypervisor not initialized");
        return -1;
    }

    if (IS_HYPERVISOR_VIDEO_HANDLE(fd)) {
        int fd_index = fd & HYPV_HANDLE_MASK;

        if ((fd_index >= MAX_HVFE_HANDLE_STORAGE) || (fd_index < 0)) {
            DEBUG_PRINT_ERROR("invalid fd=%d", fd_index);
            rc = -1;
        } else {
            //int handle_count = 0;
            pthread_mutex_lock(&g_hvfe_handle_lock);
            rc = video_fe_close(g_hvfe_handle_storage[fd_index]);
            g_hvfe_handle_storage[fd_index] = 0;
            if (--g_hvfe_handle_count == 0)
                hypv_deinit();
            pthread_mutex_unlock(&g_hvfe_handle_lock);
        }
    } else {
        rc = close(fd);
    }

    return rc;
}
