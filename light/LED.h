/*
 * Copyright (C) 2021 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <string>

namespace aidl {
namespace android {
namespace hardware {
namespace light {

class LED {
public:
    LED(std::string type);

    bool exists();
    bool setBreath(uint32_t value);
    bool setBrightness(uint32_t value);
private:
    std::string mBasePath;
    uint32_t mMaxBrightness;
    bool mBreath;
};

} // namespace light
} // namespace hardware
} // namespace android
} // namespace aidl
