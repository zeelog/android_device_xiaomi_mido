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

bool isLit(uint32_t color) {
    return color & 0x00ffffff;
}

rgb_t colorToRgb(uint32_t color) {
    rgb_t r;

    // Extract brightness from AARRGGBB.
    uint8_t alpha = (color >> 24) & 0xFF;

    // Retrieve each of the RGB colors
    r.red = (color >> 16) & 0xFF;
    r.green = (color >> 8) & 0xFF;
    r.blue = color & 0xFF;

    // Scale RGB colors if a brightness has been applied by the user
    if (alpha > 0 && alpha < 255) {
        r.red = r.red * alpha / 0xFF;
        r.green = r.green * alpha / 0xFF;
        r.blue = r.blue * alpha / 0xFF;
    }

    return r;
}

uint8_t rgbToBrightness(rgb_t c_rgb) {
    return (77 * c_rgb.red + 150 * c_rgb.green + 29 * c_rgb.blue) >> 8;
}

uint8_t colorToBrightness(uint32_t color) {
    rgb_t c_rgb = colorToRgb(color);

    return rgbToBrightness(c_rgb);
}

} // namespace light
} // namespace hardware
} // namespace android
} // namespace aidl
