/* Copyright (c) 2019 - 2020 The Linux Foundation. All rights reserved.
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

#include "LogBuffer.h"
#ifdef USE_GLIB
#include <execinfo.h>
#endif

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "LocSvc_LogBuffer"

namespace loc_util {

LogBuffer* LogBuffer::mInstance;
struct sigaction LogBuffer::mOriSigAction[NSIG];
struct sigaction LogBuffer::mNewSigAction;
mutex LogBuffer::sLock;

LogBuffer* LogBuffer::getInstance() {
    if (mInstance == nullptr) {
        lock_guard<mutex> guard(sLock);
        if (mInstance == nullptr) {
            mInstance = new LogBuffer();
        }
    }
    return mInstance;
}

LogBuffer::LogBuffer(): mLogList(TOTAL_LOG_LEVELS),
        mConfigVec(TOTAL_LOG_LEVELS, ConfigsInLevel(TIME_DEPTH_THRESHOLD_MINIMAL_IN_SEC,
                    MAXIMUM_NUM_IN_LIST, 0)) {
    loc_param_s_type log_buff_config_table[] =
    {
        {"E_LEVEL_TIME_DEPTH",      &mConfigVec[0].mTimeDepthThres,  NULL, 'n'},
        {"E_LEVEL_MAX_CAPACITY",    &mConfigVec[0].mMaxNumThres,     NULL, 'n'},
        {"W_LEVEL_TIME_DEPTH",      &mConfigVec[1].mTimeDepthThres,  NULL, 'n'},
        {"W_LEVEL_MAX_CAPACITY",    &mConfigVec[1].mMaxNumThres,     NULL, 'n'},
        {"I_LEVEL_TIME_DEPTH",      &mConfigVec[2].mTimeDepthThres,  NULL, 'n'},
        {"I_LEVEL_MAX_CAPACITY",    &mConfigVec[2].mMaxNumThres,     NULL, 'n'},
        {"D_LEVEL_TIME_DEPTH",      &mConfigVec[3].mTimeDepthThres,  NULL, 'n'},
        {"D_LEVEL_MAX_CAPACITY",    &mConfigVec[3].mMaxNumThres,     NULL, 'n'},
        {"V_LEVEL_TIME_DEPTH",      &mConfigVec[4].mTimeDepthThres,  NULL, 'n'},
        {"V_LEVEL_MAX_CAPACITY",    &mConfigVec[4].mMaxNumThres,     NULL, 'n'},
    };
    loc_read_conf(LOC_PATH_GPS_CONF_STR, log_buff_config_table,
            sizeof(log_buff_config_table)/sizeof(log_buff_config_table[0]));
    registerSignalHandler();
}

void LogBuffer::append(string& data, int level, uint64_t timestamp) {
    lock_guard<mutex> guard(mLock);
    pair<uint64_t, string> item(timestamp, data);
    mLogList.append(item, level);
    mConfigVec[level].mCurrentSize++;

    while ((timestamp - mLogList.front(level).first) > mConfigVec[level].mTimeDepthThres ||
            mConfigVec[level].mCurrentSize > mConfigVec[level].mMaxNumThres) {
        mLogList.pop(level);
        mConfigVec[level].mCurrentSize--;
    }
}

//Dump the log buffer of specific level, level = -1 to dump all the levels in log buffer.
void LogBuffer::dump(std::function<void(stringstream&)> log, int level) {
    lock_guard<mutex> guard(mLock);
    list<pair<pair<uint64_t, string>, int>> li;
    if (-1 == level) {
        li = mLogList.dump();
    } else {
        li = mLogList.dump(level);
    }
    ALOGE("Begining of dump, buffer size: %d", (int)li.size());
    stringstream ln;
    ln << "dump log buffer, level[" << level << "]" << ", buffer size: " << li.size() << endl;
    log(ln);
    for_each (li.begin(), li.end(), [&, this](const pair<pair<uint64_t, string>, int> &item){
        stringstream line;
        line << "["<<item.first.first << "] ";
        line << "Level " << mLevelMap[item.second] << ": ";
        line << item.first.second << endl;
        if (log != nullptr) {
            log(line);
        }
    });
    ALOGE("End of dump");
}

void LogBuffer::dumpToAdbLogcat() {
    dump([](stringstream& line){
        ALOGE("%s", line.str().c_str());
    });
}

void LogBuffer::dumpToLogFile(string filePath) {
    ALOGE("Dump GPS log buffer to file: %s", filePath.c_str());
    fstream s;
    s.open(filePath, std::fstream::out | std::fstream::app);
    dump([&s](stringstream& line){
        s << line.str();
    });
    s.close();
}

void LogBuffer::flush() {
    mLogList.flush();
}

void LogBuffer::registerSignalHandler() {
    ALOGE("Singal handler registered");
    mNewSigAction.sa_sigaction = &LogBuffer::signalHandler;
    mNewSigAction.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&mNewSigAction.sa_mask);

    sigaction(SIGINT, &mNewSigAction, &mOriSigAction[SIGINT]);
    sigaction(SIGKILL, &mNewSigAction, &mOriSigAction[SIGKILL]);
    sigaction(SIGSEGV, &mNewSigAction, &mOriSigAction[SIGSEGV]);
    sigaction(SIGABRT, &mNewSigAction, &mOriSigAction[SIGABRT]);
    sigaction(SIGTRAP, &mNewSigAction, &mOriSigAction[SIGTRAP]);
    sigaction(SIGUSR1, &mNewSigAction, &mOriSigAction[SIGUSR1]);
}

void LogBuffer::signalHandler(const int code, siginfo_t *const si, void *const sc) {
    ALOGE("[Gnss Log buffer]Singal handler, signal ID: %d", code);

#ifdef USE_GLIB
    int nptrs;
    void *buffer[100];
    char **strings;

    nptrs = backtrace(buffer, sizeof(buffer)/sizeof(*buffer));
    strings = backtrace_symbols(buffer, nptrs);
    if (strings != NULL) {
        timespec tv;
        clock_gettime(CLOCK_BOOTTIME, &tv);
        uint64_t elapsedTime = (uint64_t)tv.tv_sec + (uint64_t)tv.tv_nsec/1000000000;
        for (int i = 0; i < nptrs; i++) {
            string s(strings[i]);
            mInstance->append(s, 0, elapsedTime);
        }
    }
#endif
    //Dump the log buffer to adb logcat
    mInstance->dumpToAdbLogcat();

    //Dump the log buffer to file
    time_t now = time(NULL);
    struct tm *curr_time = localtime(&now);
    char path[50];
    snprintf(path, 50, LOG_BUFFER_FILE_PATH "gpslog_%d%d%d-%d%d%d.log",
            (1900 + curr_time->tm_year), ( 1 + curr_time->tm_mon), curr_time->tm_mday,
            curr_time->tm_hour, curr_time->tm_min, curr_time->tm_sec);

    mInstance->dumpToLogFile(path);

    //Process won't be terminated if SIGUSR1 is recieved
    if (code != SIGUSR1) {
        mOriSigAction[code].sa_sigaction(code, si, sc);
    }
}

}
