/* Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
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
 *
 */

#ifndef __QCAMERA_COMMON_H__
#define __QCAMERA_COMMON_H__

#include <utils/Timers.h>

// Camera dependencies
#include "cam_types.h"
#include "cam_intf.h"

namespace qcamera {

#define ALIGN(a, b) (((a) + (b)) & ~(b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

class QCameraCommon {
public:
    QCameraCommon();
    ~QCameraCommon();

    int32_t init(cam_capability_t *cap);

    int32_t getAnalysisInfo(
        bool fdVideoEnabled, bool hal3, cam_feature_mask_t featureMask,
        cam_analysis_info_t *pAnalysisInfo);
    static uint32_t calculateLCM(int32_t num1, int32_t num2);
    static nsecs_t getBootToMonoTimeOffset();

private:
    cam_capability_t *m_pCapability;

};

}; // namespace qcamera
#endif /* __QCAMERA_COMMON_H__ */

