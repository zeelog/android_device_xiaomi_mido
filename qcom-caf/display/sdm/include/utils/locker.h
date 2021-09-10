/*
* Copyright (c) 2014 - 2016, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted
* provided that the following conditions are met:
*    * Redistributions of source code must retain the above copyright notice, this list of
*      conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above copyright notice, this list of
*      conditions and the following disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its contributors may be used to
*      endorse or promote products derived from this software without specific prior written
*      permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __LOCKER_H__
#define __LOCKER_H__

#include <stdint.h>
#include <pthread.h>
#include <sys/time.h>

#define SCOPE_LOCK(locker) Locker::ScopeLock lock(locker)
#define SEQUENCE_ENTRY_SCOPE_LOCK(locker) Locker::SequenceEntryScopeLock lock(locker)
#define SEQUENCE_EXIT_SCOPE_LOCK(locker) Locker::SequenceExitScopeLock lock(locker)
#define SEQUENCE_WAIT_SCOPE_LOCK(locker) Locker::SequenceWaitScopeLock lock(locker)
#define SEQUENCE_CANCEL_SCOPE_LOCK(locker) Locker::SequenceCancelScopeLock lock(locker)

namespace sdm {

class Locker {
 public:
  class ScopeLock {
   public:
    explicit ScopeLock(Locker& locker) : locker_(locker) {
      locker_.Lock();
    }

    ~ScopeLock() {
      locker_.Unlock();
    }

   private:
    Locker &locker_;
  };

  class SequenceEntryScopeLock {
   public:
    explicit SequenceEntryScopeLock(Locker& locker) : locker_(locker) {
      locker_.Lock();
      locker_.sequence_wait_ = 1;
    }

    ~SequenceEntryScopeLock() {
      locker_.Unlock();
    }

   private:
    Locker &locker_;
  };

  class SequenceExitScopeLock {
   public:
    explicit SequenceExitScopeLock(Locker& locker) : locker_(locker) {
      locker_.Lock();
      locker_.sequence_wait_ = 0;
    }

    ~SequenceExitScopeLock() {
      locker_.Broadcast();
      locker_.Unlock();
    }

   private:
    Locker &locker_;
  };

  class SequenceWaitScopeLock {
   public:
    explicit SequenceWaitScopeLock(Locker& locker) : locker_(locker), error_(false) {
      locker_.Lock();

      while (locker_.sequence_wait_ == 1) {
        locker_.Wait();
        error_ = (locker_.sequence_wait_ == -1);
      }
    }

    ~SequenceWaitScopeLock() {
      locker_.Unlock();
    }

    bool IsError() {
      return error_;
    }

   private:
    Locker &locker_;
    bool error_;
  };

  class SequenceCancelScopeLock {
   public:
    explicit SequenceCancelScopeLock(Locker& locker) : locker_(locker) {
      locker_.Lock();
      locker_.sequence_wait_ = -1;
    }

    ~SequenceCancelScopeLock() {
      locker_.Broadcast();
      locker_.Unlock();
    }

   private:
    Locker &locker_;
  };

  Locker() : sequence_wait_(0) {
    pthread_mutex_init(&mutex_, 0);
    pthread_cond_init(&condition_, 0);
  }

  ~Locker() {
    pthread_mutex_destroy(&mutex_);
    pthread_cond_destroy(&condition_);
  }

  void Lock() { pthread_mutex_lock(&mutex_); }
  void Unlock() { pthread_mutex_unlock(&mutex_); }
  void Signal() { pthread_cond_signal(&condition_); }
  void Broadcast() { pthread_cond_broadcast(&condition_); }
  void Wait() { pthread_cond_wait(&condition_, &mutex_); }
  int WaitFinite(int ms) {
    struct timespec ts;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    ts.tv_sec = tv.tv_sec + ms/1000;
    ts.tv_nsec = tv.tv_usec*1000 + (ms%1000)*1000000;
    ts.tv_sec += ts.tv_nsec/1000000000L;
    ts.tv_nsec %= 1000000000L;
    return pthread_cond_timedwait(&condition_, &mutex_, &ts);
  }

 private:
  pthread_mutex_t mutex_;
  pthread_cond_t condition_;
  int sequence_wait_;   // This flag is set to 1 on sequence entry, 0 on exit, and -1 on cancel.
                        // Some routines will wait for sequence of function calls to finish
                        // so that capturing a transitionary snapshot of context is prevented.
                        // If flag is set to -1, these routines will exit without doing any
                        // further processing.
};

}  // namespace sdm

#endif  // __LOCKER_H__

