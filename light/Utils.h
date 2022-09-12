/*
 * Copyright (C) 2021-2022 The LineageOS Project
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
    uint8_t alpha;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} argb_t;

bool fileWriteable(const std::string& file);
bool readFromFile(const std::string& file, std::string *content);
bool readFromFile(const std::string& file, uint32_t *content);
bool writeToFile(const std::string& file, uint32_t content);
bool isLit(uint32_t color);
argb_t colorToArgb(uint32_t color);
uint8_t argbToBrightness(argb_t c_argb);
uint8_t colorToBrightness(uint32_t color);

} // namespace light
} // namespace hardware
} // namespace android
} // namespace aidl
