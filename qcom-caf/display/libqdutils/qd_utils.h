/*
 * Copyright (C) 2013, 2017 The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation or the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _QD_UTIL_MISC_H
#define _QD_UTIL_MISC_H

#include <utils/threads.h>
#include <linux/fb.h>
#include <ctype.h>
#include <fcntl.h>
#include <utils/Errors.h>
#include <log/log.h>

#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/resource.h>
#include <cutils/properties.h>
#include <hardware/hwcomposer.h>

namespace qdutils {

enum HWQueryType {
    HAS_UBWC = 1,
    HAS_WB_UBWC = 2
};

enum {
    EDID_RAW_DATA_SIZE = 640,
    MAX_FRAME_BUFFER_NAME_SIZE = 128,
    MAX_SYSFS_FILE_PATH = 255,
    MAX_STRING_LENGTH = 1024,
};

int querySDEInfo(HWQueryType type, int *value);
int getEdidRawData(char *buffer);
int getHDMINode(void);
bool isDPConnected();
int getDPTestConfig(uint32_t *panelBpp, uint32_t *patternType);

enum class DriverType {
    FB = 0,
    DRM,
};
DriverType getDriverType();
const char *GetHALPixelFormatString(int format);
static const int kFBNodeMax = 4;
}; //namespace qdutils
#endif
