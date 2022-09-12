/*
 * Copyright (C) 2021-2022 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "Utils.h"

#define LOG_TAG "android.hardware.light-service.xiaomi"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <unistd.h>

using ::android::base::ReadFileToString;
using ::android::base::WriteStringToFile;

namespace aidl {
namespace android {
namespace hardware {
namespace light {

bool fileWriteable(const std::string& file) {
    return !access(file.c_str(), W_OK);
}

bool readFromFile(const std::string& file, std::string *content) {
    return ReadFileToString(file, content, true);
}

bool readFromFile(const std::string& file, uint32_t *content) {
    std::string content_str;
    if (readFromFile(file, &content_str))
        *content = std::stoi(content_str);
    else
        return false;
    return true;
}

bool writeToFile(const std::string& file, std::string content) {
    return WriteStringToFile(content, file);
}

bool writeToFile(const std::string& file, uint32_t content) {
    return writeToFile(file, std::to_string(content));
}

rgb::rgb(uint32_t color) {
    // Extract brightness from AARRGGBB.
    uint8_t alpha = (color >> 24) & 0xFF;

    // Retrieve each of the RGB colors
    red = (color >> 16) & 0xFF;
    green = (color >> 8) & 0xFF;
    blue = color & 0xFF;

    // Scale RGB colors if a brightness has been applied by the user
    if (alpha > 0 && alpha < 255) {
        red = red * alpha / 0xFF;
        green = green * alpha / 0xFF;
        blue = blue * alpha / 0xFF;
    }
}

bool rgb::isLit() {
    return !!red || !!green || !!blue;
}

uint8_t rgb::toBrightness() {
    return (77 * red + 150 * green + 29 * blue) >> 8;
}

} // namespace light
} // namespace hardware
} // namespace android
} // namespace aidl
