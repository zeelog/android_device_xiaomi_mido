/*
 * Copyright (C) 2022 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "Backlight.h"

#include "LED.h"

namespace aidl {
namespace android {
namespace hardware {
namespace light {

class BacklightBrightness : public BacklightDevice {
public:
    BacklightBrightness(std::string name) : mBasePath(mkBacklightBasePath + name + "/") {
        if (!readFromFile(mBasePath + "max_brightness", &mMaxBrightness)) {
            mMaxBrightness = kDefaultMaxBrightness;
        }
    };

    void setBacklight(uint8_t value) {
        writeToFile(mBasePath + "brightness", value * mMaxBrightness / 0xFF);
    }

    bool exists() {
        return fileWriteable(mBasePath + "brightness");
    }
private:
    std::string mBasePath;
    uint32_t mMaxBrightness;

    inline static const std::string mkBacklightBasePath = "/sys/class/backlight/";
    inline static const uint32_t kDefaultMaxBrightness = 255;
};

class LEDBacklight : public BacklightDevice {
public:
    LEDBacklight(std::string type) : mLED(type) {};

    void setBacklight(uint8_t value) {
        mLED.setBrightness(value);
    }

    bool exists() {
        return mLED.exists();
    }
private:
    LED mLED;
};

static const std::string kBacklightDevices[] = {
    "backlight",
    "panel0-backlight",
};

static const std::string kLedDevices[] = {
    "lcd-backlight",
};

BacklightDevice *getBacklightDevice() {
    for (auto &device : kBacklightDevices) {
        auto backlight = new BacklightBrightness(device);
        if (backlight->exists()) {
            return backlight;
        }
        delete backlight;
    }

    for (auto& device : kLedDevices) {
        auto backlight = new LEDBacklight(device);
        if (backlight->exists()) {
            return backlight;
        }
        delete backlight;
    }

    return nullptr;
}

} // namespace light
} // namespace hardware
} // namespace android
} // namespace aidl
