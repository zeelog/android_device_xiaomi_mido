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

typedef struct rgb {
    rgb(uint8_t r, uint8_t g, uint8_t b) : red(r), green(g), blue(b) {};
    rgb(uint32_t color);
    rgb() : red(0), green(0), blue(0) {};

    uint8_t red;
    uint8_t green;
    uint8_t blue;

    bool isLit();
    uint8_t toBrightness();
} rgb_t;

bool fileWriteable(const std::string& file);
bool readFromFile(const std::string& file, std::string *content);
bool readFromFile(const std::string& file, uint32_t *content);
bool writeToFile(const std::string& file, uint32_t content);

} // namespace light
} // namespace hardware
} // namespace android
} // namespace aidl
