/*
 * Copyright (C) 2021 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "LED.h"

#include "Utils.h"

namespace aidl {
namespace android {
namespace hardware {
namespace light {

static const uint32_t kDefaultMaxLedBrightness = 255;

LED::LED(std::string type) : mBasePath("/sys/class/leds/" + type + "/") {
    if (!readFromFile(mBasePath + "max_brightness", &mMaxBrightness))
        mMaxBrightness = kDefaultMaxLedBrightness;
    mBreath = fileWriteable(mBasePath + "breath");
}

bool LED::exists() {
    return fileWriteable(mBasePath + "brightness");
}

bool LED::setBreath(uint32_t value) {
    return writeToFile(mBasePath + (mBreath ? "breath" : "blink"), value);
}

bool LED::setBrightness(uint32_t value) {
    return writeToFile(mBasePath + "brightness", value * mMaxBrightness / 0xFF);
}

} // namespace light
} // namespace hardware
} // namespace android
} // namespace aidl
