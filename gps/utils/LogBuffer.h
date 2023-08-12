/* Copyright (c) 2019 The Linux Foundation. All rights reserved.
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

#ifndef LOG_BUFFER_H
#define LOG_BUFFER_H

#include "SkipList.h"
#include "log_util.h"
#include <loc_cfg.h>
#include <loc_pla.h>
#include <string>
#include <sstream>
#include <ostream>
#include <fstream>
#include <time.h>
#include <mutex>
#include <signal.h>
#include <thread>
#include <functional>

//default error level time depth threshold,
#define TIME_DEPTH_THRESHOLD_MINIMAL_IN_SEC 60
//default maximum log buffer size
#define MAXIMUM_NUM_IN_LIST 50
//file path of dumped log buffer
#define LOG_BUFFER_FILE_PATH "/data/vendor/location/"

namespace loc_util {

class ConfigsInLevel{
public:
    uint32_t mTimeDepthThres;
    uint32_t mMaxNumThres;
    int mCurrentSize;

    ConfigsInLevel(uint32_t time, int num, int size):
        mTimeDepthThres(time), mMaxNumThres(num), mCurrentSize(size) {}
};

class LogBuffer {
private:
    static LogBuffer* mInstance;
    static struct sigaction mOriSigAction[NSIG];
    static struct sigaction mNewSigAction;
    static mutex sLock;

    SkipList<pair<uint64_t, string>> mLogList;
    vector<ConfigsInLevel> mConfigVec;
    mutex mLock;

    const vector<string> mLevelMap {"E", "W", "I", "D", "V"};

public:
    static LogBuffer* getInstance();
    void append(string& data, int level, uint64_t timestamp);
    void dump(std::function<void(stringstream&)> log, int level = -1);
    void dumpToAdbLogcat();
    void dumpToLogFile(string filePath);
    void flush();
private:
    LogBuffer();
    void registerSignalHandler();
    static void signalHandler(const int code, siginfo_t *const si, void *const sc);

};

}

#endif
