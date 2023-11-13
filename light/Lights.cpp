/*
 * Copyright (C) 2021-2022 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "Lights.h"

#include <android-base/logging.h>
#include "LED.h"
#include "Utils.h"

namespace aidl {
namespace android {
namespace hardware {
namespace light {

static const std::string kAllButtonsPaths[] = {
    "/sys/class/leds/button-backlight/brightness",
    "/sys/class/leds/button-backlight1/brightness",
};

enum led_type {
    RED,
    GREEN,
    BLUE,
    WHITE,
    MAX_LEDS,
};

static LED kLEDs[MAX_LEDS] = {
    [RED] = LED("red"),
    [GREEN] = LED("green"),
    [BLUE] = LED("blue"),
    [WHITE] = LED("white"),
};

#define AutoHwLight(light) {.id = (int32_t)light, .type = light, .ordinal = 0}

static const HwLight kBacklightHwLight = AutoHwLight(LightType::BACKLIGHT);
static const HwLight kBatteryHwLight = AutoHwLight(LightType::BATTERY);
static const HwLight kButtonsHwLight = AutoHwLight(LightType::BUTTONS);
static const HwLight kNotificationHwLight = AutoHwLight(LightType::NOTIFICATIONS);

Lights::Lights() {
    mBacklightDevice = getBacklightDevice();
    if (mBacklightDevice) {
        mLights.push_back(kBacklightHwLight);
    }

    for (auto& buttons : kAllButtonsPaths) {
        if (!fileWriteable(buttons))
            continue;

        mButtonsPaths.push_back(buttons);
    }

    if (!mButtonsPaths.empty())
        mLights.push_back(kButtonsHwLight);

    mWhiteLED = kLEDs[WHITE].exists();

    mLights.push_back(kBatteryHwLight);
    mLights.push_back(kNotificationHwLight);
}

ndk::ScopedAStatus Lights::setLightState(int32_t id, const HwLightState& state) {
    rgb_t color(state.color);
    rgb_t batteryStateColor;

    LightType type = static_cast<LightType>(id);
    switch (type) {
        case LightType::BACKLIGHT:
            if (mBacklightDevice)
                mBacklightDevice->setBacklight(color.toBrightness());
            break;
        case LightType::BUTTONS:
            for (auto& buttons : mButtonsPaths)
                writeToFile(buttons, color.isLit());
            break;
        case LightType::BATTERY:
        case LightType::NOTIFICATIONS:
            mLEDMutex.lock();
            if (type == LightType::BATTERY)
                mLastBatteryState = state;
            else
                mLastNotificationState = state;
            batteryStateColor = rgb_t(mLastBatteryState.color);
            setLED(batteryStateColor.isLit() ? mLastBatteryState : mLastNotificationState);
            mLEDMutex.unlock();
            break;
        default:
            return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
            break;
    }

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Lights::getLights(std::vector<HwLight> *_aidl_return) {
    for (auto& light : mLights)
        _aidl_return->push_back(light);

    return ndk::ScopedAStatus::ok();
}

void Lights::setLED(const HwLightState& state) {
    bool rc = true;
    rgb_t color(state.color);
    uint8_t blink = (state.flashOnMs != 0 && state.flashOffMs != 0);

    switch (state.flashMode) {
        case FlashMode::HARDWARE:
        case FlashMode::TIMED:
            if (mWhiteLED) {
                rc = kLEDs[WHITE].setBreath(blink);
            } else {
                rc = kLEDs[RED].setBreath(blink && color.red);
                rc &= kLEDs[GREEN].setBreath(blink && color.green);
                rc &= kLEDs[BLUE].setBreath(blink && color.blue);
            }
            if (rc)
                break;
            FALLTHROUGH_INTENDED;
        default:
            if (mWhiteLED) {
                rc = kLEDs[WHITE].setBrightness(color.toBrightness());
            } else {
                rc = kLEDs[RED].setBrightness(color.red);
                rc &= kLEDs[GREEN].setBrightness(color.green);
                rc &= kLEDs[BLUE].setBrightness(color.blue);
            }
            break;
    }

    return;
}

} // namespace light
} // namespace hardware
} // namespace android
} // namespace aidl
