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

typedef struct argb {
    uint32_t alpha;
    uint32_t red;
    uint32_t green;
    uint32_t blue;
} argb_t;

bool fileWriteable(const std::string& file);
bool readFromFile(const std::string& file, std::string *content);
bool readFromFile(const std::string& file, uint32_t *content);
bool writeToFile(const std::string& file, uint32_t content);
bool isLit(uint32_t color);
argb_t colorToArgb(uint32_t color);
uint32_t argbToBrightness(argb_t c_argb);
uint32_t colorToBrightness(uint32_t color);

} // namespace light
} // namespace hardware
} // namespace android
} // namespace aidl
