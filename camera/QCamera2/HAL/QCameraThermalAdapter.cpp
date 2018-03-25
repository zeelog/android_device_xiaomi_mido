/* Copyright (c) 2013-2016, The Linux Foundation. All rights reserved.
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
 *
 */

#define LOG_TAG "QCameraThermalAdapter"

// System dependencies
#include <dlfcn.h>
#include <utils/Errors.h>

// Camera dependencies
#include "QCamera2HWI.h"
#include "QCameraThermalAdapter.h"

extern "C" {
#include "mm_camera_dbg.h"
}

using namespace android;

namespace qcamera {


QCameraThermalAdapter& QCameraThermalAdapter::getInstance()
{
    static QCameraThermalAdapter instance;
    return instance;
}

QCameraThermalAdapter::QCameraThermalAdapter() :
                                        mCallback(NULL),
                                        mHandle(NULL),
                                        mRegister(NULL),
                                        mUnregister(NULL),
                                        mCameraHandle(0),
                                        mCamcorderHandle(0)
{
}

int QCameraThermalAdapter::init(QCameraThermalCallback *thermalCb)
{
    const char *error = NULL;
    int rc = NO_ERROR;

    LOGD("E");
    mHandle = dlopen("/vendor/lib/libthermalclient.so", RTLD_NOW);
    if (!mHandle) {
        error = dlerror();
        LOGE("dlopen failed with error %s",
                     error ? error : "");
        rc = UNKNOWN_ERROR;
        goto error;
    }
    *(void **)&mRegister = dlsym(mHandle, "thermal_client_register_callback");
    if (!mRegister) {
        error = dlerror();
        LOGE("dlsym failed with error code %s",
                     error ? error: "");
        rc = UNKNOWN_ERROR;
        goto error2;
    }
    *(void **)&mUnregister = dlsym(mHandle, "thermal_client_unregister_callback");
    if (!mUnregister) {
        error = dlerror();
        LOGE("dlsym failed with error code %s",
                     error ? error: "");
        rc = UNKNOWN_ERROR;
        goto error2;
    }

    mCallback = thermalCb;

    // Register camera and camcorder callbacks
    mCameraHandle = mRegister(mStrCamera, thermalCallback, NULL);
    if (mCameraHandle < 0) {
        LOGE("thermal_client_register_callback failed %d",
                         mCameraHandle);
        rc = UNKNOWN_ERROR;
        goto error2;
    }
    mCamcorderHandle = mRegister(mStrCamcorder, thermalCallback, NULL);
    if (mCamcorderHandle < 0) {
        LOGE("thermal_client_register_callback failed %d",
                         mCamcorderHandle);
        rc = UNKNOWN_ERROR;
        goto error3;
    }

    LOGD("X");
    return rc;

error3:
    mCamcorderHandle = 0;
    mUnregister(mCameraHandle);
error2:
    mCameraHandle = 0;
    dlclose(mHandle);
    mHandle = NULL;
error:
    LOGD("X");
    return rc;
}

void QCameraThermalAdapter::deinit()
{
    LOGD("E");
    if (mUnregister) {
        if (mCameraHandle) {
            mUnregister(mCameraHandle);
            mCameraHandle = 0;
        }
        if (mCamcorderHandle) {
            mUnregister(mCamcorderHandle);
            mCamcorderHandle = 0;
        }
    }
    if (mHandle)
        dlclose(mHandle);

    mHandle = NULL;
    mRegister = NULL;
    mUnregister = NULL;
    mCallback = NULL;
    LOGD("X");
}

char QCameraThermalAdapter::mStrCamera[] = "camera";
char QCameraThermalAdapter::mStrCamcorder[] = "camcorder";

int QCameraThermalAdapter::thermalCallback(int level,
                void *userdata, void *data)
{
    int rc = 0;
    LOGD("E");
    QCameraThermalCallback *mcb = getInstance().mCallback;

    if (mcb) {
        mcb->setThermalLevel((qcamera_thermal_level_enum_t) level);
        rc = mcb->thermalEvtHandle(mcb->getThermalLevel(), userdata, data);
    }
    LOGD("X");
    return rc;
}

qcamera_thermal_level_enum_t *QCameraThermalCallback::getThermalLevel() {
    return &mLevel;
}

void QCameraThermalCallback::setThermalLevel(qcamera_thermal_level_enum_t level) {
    mLevel = level;
}
}; //namespace qcamera
