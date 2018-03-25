/* Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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

#ifndef __QCAMERAPERF_H__
#define __QCAMERAPERF_H__

// System dependencies
#include <utils/List.h>
#include <utils/Mutex.h>

// Camera dependencies
#include "hardware/power.h"

typedef enum {
    ALL_CORES_ONLINE = 0x7FE,
    ALL_CPUS_PWR_CLPS_DIS = 0x101,
    CPU0_MIN_FREQ_TURBO_MAX = 0x2FE,
    CPU4_MIN_FREQ_TURBO_MAX = 0x1FFE,
}perf_lock_params_t;

/* Time related macros */
#define ONE_SEC 1000
typedef int64_t nsecs_t;
#define NSEC_PER_SEC 1000000000LLU

using namespace android;

namespace qcamera {

class QCameraPerfLock {
public:
    QCameraPerfLock();
    ~QCameraPerfLock();

    void    lock_init();
    void    lock_deinit();
    int32_t lock_rel();
    int32_t lock_acq();
    int32_t lock_acq_timed(int32_t timer_val);
    int32_t lock_rel_timed();
    bool    isTimerReset();
    void    powerHintInternal(power_hint_t hint, bool enable);
    void    powerHint(power_hint_t hint, bool enable);

private:
    int32_t        (*perf_lock_acq)(int, int, int[], int);
    int32_t        (*perf_lock_rel)(int);
    void            startTimer(uint32_t timer_val);
    void            resetTimer();
    void           *mDlHandle;
    uint32_t        mPerfLockEnable;
    Mutex           mLock;
    int32_t         mPerfLockHandle;        // Performance lock library handle
    int32_t         mPerfLockHandleTimed;   // Performance lock library handle
    power_module_t *m_pPowerModule;         // power module Handle
    power_hint_t    mCurrentPowerHint;
    bool            mCurrentPowerHintEnable;
    uint32_t        mTimerSet;
    uint32_t        mPerfLockTimeout;
    nsecs_t         mStartTimeofLock;
    List<power_hint_t> mActivePowerHints;   // Active/enabled power hints list
};

}; // namespace qcamera

#endif /* __QCAMREAPERF_H__ */
