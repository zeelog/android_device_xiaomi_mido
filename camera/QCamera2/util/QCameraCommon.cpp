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

#define LOG_TAG "QCameraCommon"

// System dependencies
#include <utils/Errors.h>
#include <stdlib.h>
#include <string.h>
#include <utils/Log.h>

// Camera dependencies
#include "QCameraCommon.h"

using namespace android;

namespace qcamera {

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

/*===========================================================================
 * FUNCTION   : QCameraCommon
 *
 * DESCRIPTION: default constructor of QCameraCommon
 *
 * PARAMETERS : None
 *
 * RETURN     : None
 *==========================================================================*/
QCameraCommon::QCameraCommon() :
    m_pCapability(NULL)
{
}

/*===========================================================================
 * FUNCTION   : ~QCameraCommon
 *
 * DESCRIPTION: destructor of QCameraCommon
 *
 * PARAMETERS : None
 *
 * RETURN     : None
 *==========================================================================*/
QCameraCommon::~QCameraCommon()
{
}

/*===========================================================================
 * FUNCTION   : init
 *
 * DESCRIPTION: Init function for QCameraCommon
 *
 * PARAMETERS :
 *   @pCapability : Capabilities
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCameraCommon::init(cam_capability_t *pCapability)
{
    m_pCapability = pCapability;

    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : calculateLCM
 *
 * DESCRIPTION: Get the LCM of 2 numbers
 *
 * PARAMETERS :
 *   @num1   : First number
 *   @num2   : second number
 *
 * RETURN     : int32_t type (LCM)
 *
 *==========================================================================*/
uint32_t QCameraCommon::calculateLCM(int32_t num1, int32_t num2)
{
   uint32_t lcm = 0;
   uint32_t temp = 0;

   if ((num1 < 1) && (num2 < 1)) {
       return 0;
   } else if (num1 < 1) {
       return num2;
   } else if (num2 < 1) {
       return num1;
   }

   if (num1 > num2) {
       lcm = num1;
   } else {
       lcm = num2;
   }
   temp = lcm;

   while (1) {
       if (((lcm % num1) == 0) && ((lcm % num2) == 0)) {
           break;
       }
       lcm += temp;
   }
   return lcm;
}

/*===========================================================================
 * FUNCTION   : getAnalysisInfo
 *
 * DESCRIPTION: Get the Analysis information based on
 *     current mode and feature mask
 *
 * PARAMETERS :
 *   @fdVideoEnabled : Whether fdVideo enabled currently
 *   @hal3           : Whether hal3 or hal1
 *   @featureMask    : Feature mask
 *   @pAnalysis_info : Analysis info to be filled
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCameraCommon::getAnalysisInfo(
        bool fdVideoEnabled,
        bool hal3,
        cam_feature_mask_t featureMask,
        cam_analysis_info_t *pAnalysisInfo)
{
    if (!pAnalysisInfo) {
        return BAD_VALUE;
    }

    pAnalysisInfo->valid = 0;

    if ((fdVideoEnabled == TRUE) && (hal3 == FALSE) &&
            (m_pCapability->analysis_info[CAM_ANALYSIS_INFO_FD_VIDEO].hw_analysis_supported) &&
            (m_pCapability->analysis_info[CAM_ANALYSIS_INFO_FD_VIDEO].valid)) {
        *pAnalysisInfo =
                m_pCapability->analysis_info[CAM_ANALYSIS_INFO_FD_VIDEO];
    } else if (m_pCapability->analysis_info[CAM_ANALYSIS_INFO_FD_STILL].valid) {
        *pAnalysisInfo =
                m_pCapability->analysis_info[CAM_ANALYSIS_INFO_FD_STILL];
        if (hal3 == TRUE) {
            pAnalysisInfo->analysis_max_res = pAnalysisInfo->analysis_recommended_res;
        }
    }

    if ((featureMask & CAM_QCOM_FEATURE_PAAF) &&
      (m_pCapability->analysis_info[CAM_ANALYSIS_INFO_PAAF].valid)) {
        cam_analysis_info_t *pPaafInfo =
          &m_pCapability->analysis_info[CAM_ANALYSIS_INFO_PAAF];

        if (!pAnalysisInfo->valid) {
            *pAnalysisInfo = *pPaafInfo;
        } else {
            pAnalysisInfo->analysis_max_res.width =
                MAX(pAnalysisInfo->analysis_max_res.width,
                pPaafInfo->analysis_max_res.width);
            pAnalysisInfo->analysis_max_res.height =
                MAX(pAnalysisInfo->analysis_max_res.height,
                pPaafInfo->analysis_max_res.height);
            pAnalysisInfo->analysis_padding_info.height_padding =
                calculateLCM(pAnalysisInfo->analysis_padding_info.height_padding,
                pPaafInfo->analysis_padding_info.height_padding);
            pAnalysisInfo->analysis_padding_info.width_padding =
                calculateLCM(pAnalysisInfo->analysis_padding_info.width_padding,
                pPaafInfo->analysis_padding_info.width_padding);
            pAnalysisInfo->analysis_padding_info.plane_padding =
                calculateLCM(pAnalysisInfo->analysis_padding_info.plane_padding,
                pPaafInfo->analysis_padding_info.plane_padding);
            pAnalysisInfo->analysis_padding_info.min_stride =
                MAX(pAnalysisInfo->analysis_padding_info.min_stride,
                pPaafInfo->analysis_padding_info.min_stride);
            pAnalysisInfo->analysis_padding_info.min_stride =
                ALIGN(pAnalysisInfo->analysis_padding_info.min_stride,
                pAnalysisInfo->analysis_padding_info.width_padding);

            pAnalysisInfo->analysis_padding_info.min_scanline =
                MAX(pAnalysisInfo->analysis_padding_info.min_scanline,
                pPaafInfo->analysis_padding_info.min_scanline);
            pAnalysisInfo->analysis_padding_info.min_scanline =
                ALIGN(pAnalysisInfo->analysis_padding_info.min_scanline,
                pAnalysisInfo->analysis_padding_info.height_padding);

            pAnalysisInfo->hw_analysis_supported |=
                pPaafInfo->hw_analysis_supported;
        }
    }

    return pAnalysisInfo->valid ? NO_ERROR : BAD_VALUE;
}

/*===========================================================================
 * FUNCTION   : getBootToMonoTimeOffset
 *
 * DESCRIPTION: Calculate offset that is used to convert from
 *              clock domain of boot to monotonic
 *
 * PARAMETERS :
 *   None
 *
 * RETURN     : clock offset between boottime and monotonic time.
 *
 *==========================================================================*/
nsecs_t QCameraCommon::getBootToMonoTimeOffset()
{
    // try three times to get the clock offset, choose the one
    // with the minimum gap in measurements.
    const int tries = 3;
    nsecs_t bestGap, measured;
    for (int i = 0; i < tries; ++i) {
        const nsecs_t tmono = systemTime(SYSTEM_TIME_MONOTONIC);
        const nsecs_t tbase = systemTime(SYSTEM_TIME_BOOTTIME);
        const nsecs_t tmono2 = systemTime(SYSTEM_TIME_MONOTONIC);
        const nsecs_t gap = tmono2 - tmono;
        if (i == 0 || gap < bestGap) {
            bestGap = gap;
            measured = tbase - ((tmono + tmono2) >> 1);
        }
    }
    return measured;
}

}; // namespace qcamera
