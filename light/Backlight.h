/*
 * Copyright (C) 2022 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "Utils.h"

namespace aidl {
namespace android {
namespace hardware {
namespace light {

class BacklightDevice {
public:
    virtual ~BacklightDevice() = default;

    virtual void setBacklight(uint8_t value) = 0;
    virtual bool exists() = 0;
};

BacklightDevice *getBacklightDevice();

} // namespace light
} // namespace hardware
} // namespace android
} // namespace aidl
