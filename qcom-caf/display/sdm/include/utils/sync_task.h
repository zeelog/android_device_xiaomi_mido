/*
* Copyright (c) 2017, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*  * Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
*  * Redistributions in binary form must reproduce the above
*    copyright notice, this list of conditions and the following
*    disclaimer in the documentation and/or other materials provided
*    with the distribution.
*  * Neither the name of The Linux Foundation nor the names of its
*    contributors may be used to endorse or promote products derived
*    from this software without specific prior written permission.
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
*/

#ifndef __SYNC_TASK_H__
#define __SYNC_TASK_H__

#include <thread>
#include <mutex>
#include <condition_variable>   // NOLINT

namespace sdm {

template <class TaskCode>
class SyncTask {
 public:
  // This class need to be overridden by caller to pass on a task context.
  class TaskContext {
   public:
    virtual ~TaskContext() { }
  };

  // Methods to callback into caller for command codes executions in worker thread.
  class TaskHandler {
   public:
    virtual ~TaskHandler() { }
    virtual void OnTask(const TaskCode &task_code, TaskContext *task_context) = 0;
  };

  explicit SyncTask(TaskHandler &task_handler) : task_handler_(task_handler) {
    // Block caller thread until worker thread has started and ready to listen to task commands.
    // Worker thread will signal as soon as callback is received in the new thread.
    std::unique_lock<std::mutex> caller_lock(caller_mutex_);
    std::thread worker_thread(SyncTaskThread, this);
    worker_thread_.swap(worker_thread);
    caller_cv_.wait(caller_lock);
  }

  ~SyncTask() {
    // Task code does not matter here.
    PerformTask(task_code_, nullptr, true);
    worker_thread_.join();
  }

  void PerformTask(const TaskCode &task_code, TaskContext *task_context) {
    PerformTask(task_code, task_context, false);
  }

 private:
  void PerformTask(const TaskCode &task_code, TaskContext *task_context, bool terminate) {
    std::unique_lock<std::mutex> caller_lock(caller_mutex_);

    // New scope to limit scope of worker lock to this block.
    {
      // Set task command code and notify worker thread.
      std::unique_lock<std::mutex> worker_lock(worker_mutex_);
      task_code_ = task_code;
      task_context_ = task_context;
      worker_thread_exit_ = terminate;
      pending_code_ = true;
      worker_cv_.notify_one();
    }

    // Wait for worker thread to finish and signal.
    caller_cv_.wait(caller_lock);
  }

  static void SyncTaskThread(SyncTask *sync_task) {
    if (sync_task) {
      sync_task->OnThreadCallback();
    }
  }

  void OnThreadCallback() {
    // Acquire worker lock and start waiting for events.
    // Wait must start before caller thread can post events, otherwise posted events will be lost.
    // Caller thread will be blocked until worker thread signals readiness.
    std::unique_lock<std::mutex> worker_lock(worker_mutex_);

    // New scope to limit scope of caller lock to this block.
    {
      // Signal caller thread that worker thread is ready to listen to events.
      std::unique_lock<std::mutex> caller_lock(caller_mutex_);
      caller_cv_.notify_one();
    }

    while (!worker_thread_exit_) {
      // Add predicate to handle spurious interrupts.
      // Wait for caller thread to signal new command codes.
      worker_cv_.wait(worker_lock, [this] { return pending_code_; });

      // Call task handler which is implemented by the caller.
      if (!worker_thread_exit_) {
        task_handler_.OnTask(task_code_, task_context_);
      }

      pending_code_ = false;
      // Notify completion of current task to the caller thread which is blocked.
      std::unique_lock<std::mutex> caller_lock(caller_mutex_);
      caller_cv_.notify_one();
    }
  }

  TaskHandler &task_handler_;
  TaskCode task_code_;
  TaskContext *task_context_ = nullptr;
  std::thread worker_thread_;
  std::mutex caller_mutex_;
  std::mutex worker_mutex_;
  std::condition_variable caller_cv_;
  std::condition_variable worker_cv_;
  bool worker_thread_exit_ = false;
  bool pending_code_ = false;
};

}  // namespace sdm

#endif  // __SYNC_TASK_H__
