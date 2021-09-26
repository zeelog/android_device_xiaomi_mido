/*
 * Copyright (C) 2021 The LineageOS Project
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

argb_t colorToArgb(uint32_t color) {
    argb_t r;

    // Extract brightness from AARRGGBB.
    r.alpha = (color >> 24) & 0xFF;

    // Retrieve each of the RGB colors
    r.red = (color >> 16) & 0xFF;
    r.green = (color >> 8) & 0xFF;
    r.blue = color & 0xFF;

    // Scale RGB colors if a brightness has been applied by the user
    if (r.alpha > 0 && r.alpha < 255) {
        r.red = r.red * r.alpha / 0xFF;
        r.green = r.green * r.alpha / 0xFF;
        r.blue = r.blue * r.alpha / 0xFF;
    }

    return r;
}

uint32_t argbToBrightness(argb_t c_argb) {
    return (77 * c_argb.red + 150 * c_argb.green + 29 * c_argb.blue) >> 8;
}

uint32_t colorToBrightness(uint32_t color) {
    argb_t c_argb = colorToArgb(color);

    return argbToBrightness(c_argb);
}

} // namespace light
} // namespace hardware
} // namespace android
} // namespace aidl
