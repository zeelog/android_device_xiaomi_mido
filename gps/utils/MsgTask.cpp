/* Copyright (c) 2011-2013, 2015, 2017, 2020 The Linux Foundation. All rights reserved.
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
 *     * Neither the name of The Linux Foundation, nor the names of its
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
#define LOG_NDEBUG 0
#define LOG_TAG "LocSvc_MsgTask"

#include <unistd.h>
#include <MsgTask.h>
#include <msg_q.h>
#include <log_util.h>
#include <loc_log.h>
#include <loc_pla.h>

namespace loc_util {

class MTRunnable : public LocRunnable {
    const void* mQ;
public:
    inline MTRunnable(const void* q) : mQ(q) {}
    virtual ~MTRunnable();
    // Overrides of LocRunnable methods
    // This method will be repeated called until it returns false; or
    // until thread is stopped.
    virtual bool run() override;

    // The method to be run before thread loop (conditionally repeatedly)
    // calls run()
    virtual void prerun() override;

    // to interrupt the run() method and come out of that
    virtual void interrupt() override;
};

static void LocMsgDestroy(void* msg) {
    delete (LocMsg*)msg;
}

MsgTask::MsgTask(const char* threadName) :
    mQ(msg_q_init2()), mThread() {
    mThread.start(threadName, std::make_shared<MTRunnable>(mQ));
}

void MsgTask::sendMsg(const LocMsg* msg) const {
    if (msg && this) {
        msg_q_snd((void*)mQ, (void*)msg, LocMsgDestroy);
    } else {
        LOC_LOGE("%s: msg is %p and this is %p",
                 __func__, msg, this);
    }
}

void MsgTask::sendMsg(const std::function<void()> runnable) const {
    struct RunMsg : public LocMsg {
        const std::function<void()> mRunnable;
    public:
        inline RunMsg(const std::function<void()> runnable) : mRunnable(runnable) {}
        ~RunMsg() = default;
        inline virtual void proc() const override { mRunnable(); }
    };
    sendMsg(new RunMsg(runnable));
}

void MTRunnable::interrupt() {
    msg_q_unblock((void*)mQ);
}

void MTRunnable::prerun() {
    // make sure we do not run in background scheduling group
     set_sched_policy(gettid(), SP_FOREGROUND);
}

bool MTRunnable::run() {
    LocMsg* msg;
    msq_q_err_type result = msg_q_rcv((void*)mQ, (void **)&msg);
    if (eMSG_Q_SUCCESS != result) {
        LOC_LOGE("%s:%d] fail receiving msg: %s\n", __func__, __LINE__,
                 loc_get_msg_q_status(result));
        return false;
    }

    msg->log();
    // there is where each individual msg handling is invoked
    msg->proc();

    delete msg;

    return true;
}

MTRunnable::~MTRunnable() {
    msg_q_flush((void*)mQ);
    msg_q_destroy((void**)&mQ);
}

} // namespace loc_util
