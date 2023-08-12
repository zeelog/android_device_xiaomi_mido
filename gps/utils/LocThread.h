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
#ifndef __LOC_THREAD__
#define __LOC_THREAD__

#include <stddef.h>
#include <memory>

using std::shared_ptr;

namespace loc_util {

// abstract class to be implemented by client to provide a runnable class
// which gets scheduled by LocThread
class LocRunnable {
public:
    inline LocRunnable() = default;
    inline virtual ~LocRunnable() = default;

    // The method to be implemented by thread clients
    // and be scheduled by LocThread
    // This method will be repeated called until it returns false; or
    // until thread is stopped.
    virtual bool run() = 0;

    // The method to be run before thread loop (conditionally repeatedly)
    // calls run()
    inline virtual void prerun() {}

    // The method to be run after thread loop (conditionally repeatedly)
    // calls run()
    inline virtual void postrun() {}

    // The method to wake up the potential blocking thread
    // no op if not applicable
    inline virtual void interrupt() = 0;
};

// opaque class to provide service implementation.
class LocThreadDelegate;

// A utility class to create a thread and run LocRunnable
// caller passes in.
class LocThread {
    LocThreadDelegate* mThread;
public:
    inline LocThread() : mThread(NULL) {}
    inline virtual ~LocThread() { stop(); }

    // client starts thread with a runnable, which implements
    // the logics to fun in the created thread context.
    // The thread is always detached.
    // runnable is an obj managed by client. Client creates and
    //          frees it (but must be after stop() is called, or
    //          this LocThread obj is deleted).
    //          The obj will be deleted by LocThread if start()
    //          returns true. Else it is client's responsibility
    //          to delete the object
    // Returns 0 if success; false if failure.
    bool start(const char* threadName, shared_ptr<LocRunnable> runnable);

    void stop();

    // thread status check
    inline bool isRunning() { return NULL != mThread; }
};

} // loc_util
#endif //__LOC_THREAD__
