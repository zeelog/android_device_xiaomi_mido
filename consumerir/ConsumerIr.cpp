/*
 * Copyright (C) 2017-2018 The LineageOS Project
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

#define LOG_TAG "ConsumerIrService"

#include <fcntl.h>
#include <linux/lirc.h>

#include <android-base/logging.h>

#include "ConsumerIr.h"

namespace android {
namespace hardware {
namespace ir {
namespace V1_0 {
namespace implementation {

#define LIRC_DEV_PATH "/dev/lirc0"

static const int dutyCycle = 33;

static hidl_vec<ConsumerIrFreqRange> rangeVec{
    {.min = 30000, .max = 60000},
};

static int openLircDev() {
    int fd = open(LIRC_DEV_PATH, O_RDWR);

    if (fd < 0) {
        LOG(ERROR) << "failed to open " << LIRC_DEV_PATH << ", error " << fd;
    }

    return fd;
}

// Methods from ::android::hardware::ir::V1_0::IConsumerIr follow.
Return<bool> ConsumerIr::transmit(int32_t carrierFreq, const hidl_vec<int32_t>& pattern) {
    size_t entries = pattern.size();
    int rc;
    int lircFd;

    lircFd = openLircDev();
    if (lircFd < 0) {
        return lircFd;
    }

    rc = ioctl(lircFd, LIRC_SET_SEND_CARRIER, &carrierFreq);
    if (rc < 0) {
        LOG(ERROR) << "failed to set carrier " << carrierFreq << ", error: " << errno;
        goto out_close;
    }

    rc = ioctl(lircFd, LIRC_SET_SEND_DUTY_CYCLE, &dutyCycle);
    if (rc < 0) {
        LOG(ERROR) << "failed to set duty cycle " << dutyCycle << ", error: " << errno;
        goto out_close;
    }

    if ((entries & 1) != 0) {
        rc = write(lircFd, pattern.data(), sizeof(int32_t) * entries);
    } else {
        rc = write(lircFd, pattern.data(), sizeof(int32_t) * (entries - 1));
        usleep(pattern[entries - 1]);
    }

    if (rc < 0) {
        LOG(ERROR) << "failed to write pattern " << pattern.size() << ", error: " << errno;
        goto out_close;
    }

    rc = 0;

out_close:
    close(lircFd);

    return rc == 0;
}

Return<void> ConsumerIr::getCarrierFreqs(getCarrierFreqs_cb _hidl_cb) {
    _hidl_cb(true, rangeVec);
    return Void();
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace ir
}  // namespace hardware
}  // namespace android
