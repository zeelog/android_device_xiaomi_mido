/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
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

#ifndef __QCAMERADISPLAY_H__
#define __QCAMERADISPLAY_H__
#ifdef USE_DISPLAY_SERVICE
#ifdef LIKELY
#undef LIKELY
#undef UNLIKELY
#endif //LIKELY
#include <android/frameworks/displayservice/1.0/IDisplayService.h>
#include <android/frameworks/displayservice/1.0/IEventCallback.h>
#include <android/frameworks/displayservice/1.0/IDisplayEventReceiver.h>
#include <utils/Looper.h>

using ::android::frameworks::displayservice::V1_0::IDisplayEventReceiver;
using ::android::frameworks::displayservice::V1_0::IDisplayService;
using ::android::frameworks::displayservice::V1_0::IEventCallback;
using ::android::frameworks::displayservice::V1_0::Status;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::sp;
#else //USE_DISPLAY_SERVICE
#include <utils/Timers.h>
#include <gui/DisplayEventReceiver.h>
#endif //USE_DISPLAY_SERVICE

namespace qcamera {

#define CAMERA_NUM_VSYNC_INTERVAL_HISTORY  8
#define NSEC_PER_MSEC 1000000LLU

#ifdef USE_DISPLAY_SERVICE
class QCameraDisplay : public IEventCallback {
#else //USE_DISPLAY_SERVICE
class QCameraDisplay {
#endif //USE_DISPLAY_SERVICE

public:
    QCameraDisplay();
    ~QCameraDisplay();

#ifdef USE_DISPLAY_SERVICE
    void        init();
    bool        isInited() { return m_bInitDone; }
    bool        isSyncing() {return m_bSyncing; }

    bool        startVsync(bool start);

    Return<void> onVsync(uint64_t timestamp, uint32_t count) override {
        ALOGV("onVsync: timestamp=%llu count=%d", timestamp, count);
        computeAverageVsyncInterval(timestamp);
        return Void();
    }
    Return<void> onHotplug(uint64_t timestamp, bool connected) override {
        ALOGV("onHotplug: timestamp=%llu connected=%s", timestamp, connected ? "true" : "false");
        return Void();
    }

#else //USE_DISPLAY_SERVICE
    static int   vsyncEventReceiverCamera(int fd, int events, void* data);
    static void* vsyncThreadCamera(void * data);
#endif //USE_DISPLAY_SERVICE
    void        computeAverageVsyncInterval(nsecs_t currentVsyncTimeStamp);
    nsecs_t     computePresentationTimeStamp(nsecs_t frameTimeStamp);
private:
    nsecs_t   mVsyncTimeStamp;
    nsecs_t   mAvgVsyncInterval;
    nsecs_t   mOldTimeStamp;
    nsecs_t   mVsyncIntervalHistory[CAMERA_NUM_VSYNC_INTERVAL_HISTORY];
    nsecs_t   mVsyncHistoryIndex;
    nsecs_t   mAdditionalVsyncOffsetForWiggle;
    // Tunable property. Increasing this will increase the frame delay and will loose
    // the real time display.
    uint32_t  mNum_vsync_from_vfe_isr_to_presentation_timestamp;
    // Tunable property. Set the time stamp x ns prior to expected vsync so that
    // it will be picked in that vsync
    nsecs_t  mSet_timestamp_num_ns_prior_to_vsync;
    // Tunable property for filtering timestamp wiggle when VFE ISR crosses
    // over MDP ISR over a period. Typical scenario is VFE is running at
    // 30.2 fps vs display running at 60 fps.
    nsecs_t  mVfe_and_mdp_freq_wiggle_filter_max_ns;
    nsecs_t  mVfe_and_mdp_freq_wiggle_filter_min_ns;
#ifdef USE_DISPLAY_SERVICE
    bool     m_bInitDone;
    bool     m_bSyncing;
    sp<IDisplayEventReceiver> mDisplayEventReceiver;
    sp<IDisplayService> mDisplayService;
#else //USE_DISPLAY_SERVICE
    pthread_t mVsyncThreadCameraHandle;
    uint32_t  mThreadExit;
    android::DisplayEventReceiver  mDisplayEventReceiver;
#endif //USE_DISPLAY_SERVICE

};

}; // namespace qcamera

#endif /* __QCAMERADISPLAY_H__ */
