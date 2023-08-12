/* Copyright (c) 2015, 2020 The Linux Foundation. All rights reserved.
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
#include <sys/prctl.h>
#include <LocThread.h>
#include <string.h>
#include <string>
#include <thread>
#include <loc_pla.h>

using std::weak_ptr;
using std::shared_ptr;
using std::thread;
using std::string;

namespace loc_util {

class LocThreadDelegate {
    static const char defaultThreadName[];
    weak_ptr<LocRunnable> mRunnable;
    thread mThread;
    LocThreadDelegate(const string tName, shared_ptr<LocRunnable> r);
public:
    ~LocThreadDelegate() {
        shared_ptr<LocRunnable> runnable = mRunnable.lock();
        if (nullptr != runnable) {
            runnable->interrupt();
        }
    }
    inline static LocThreadDelegate* create(const char* tName, shared_ptr<LocRunnable> runnable);
};

const char LocThreadDelegate::defaultThreadName[] = "LocThread";

LocThreadDelegate* LocThreadDelegate::create(const char* tName, shared_ptr<LocRunnable> runnable) {
    LocThreadDelegate* threadDelegate = nullptr;

    if (nullptr != runnable) {
        if (!tName) {
            tName = defaultThreadName;
        }

        char lname[16];
        auto nameSize = strlen(tName) + 1;
        int len = std::min(sizeof(lname), nameSize) - 1;
        memcpy(lname, tName, len);
        lname[len] = 0;

        threadDelegate = new LocThreadDelegate(lname, runnable);
    }

    return threadDelegate;
}

LocThreadDelegate::LocThreadDelegate(const string tName, shared_ptr<LocRunnable> runnable) :
        mRunnable(runnable),
        mThread([tName, runnable] {
                prctl(PR_SET_NAME, tName.c_str(), 0, 0, 0);
                runnable->prerun();
                while (runnable->run());
                runnable->postrun();
            }) {

    mThread.detach();
}



bool LocThread::start(const char* tName, shared_ptr<LocRunnable> runnable) {
    bool success = false;
    if (!mThread) {
        mThread = LocThreadDelegate::create(tName, runnable);
        // true only if thread is created successfully
        success = (NULL != mThread);
    }
    return success;
}

void LocThread::stop() {
    if (nullptr != mThread) {
        delete mThread;
        mThread = nullptr;
    }
}

} // loc_util
