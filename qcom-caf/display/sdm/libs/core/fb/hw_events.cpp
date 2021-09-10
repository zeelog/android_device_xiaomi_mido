/*
* Copyright (c) 2015 - 2017, The Linux Foundation. All rights reserved.
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
*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <utils/debug.h>
#include <utils/sys.h>
#include <pthread.h>
#include <algorithm>
#include <vector>
#include <map>
#include <utility>

#include "hw_events.h"

#define __CLASS__ "HWEvents"

namespace sdm {

pollfd HWEvents::InitializePollFd(HWEventData *event_data) {
  char node_path[kMaxStringLength] = {0};
  char data[kMaxStringLength] = {0};
  pollfd poll_fd = {0};
  poll_fd.fd = -1;

  if (event_data->event_type == HWEvent::EXIT) {
    // Create an eventfd to be used to unblock the poll system call when
    // a thread is exiting.
    poll_fd.fd = Sys::eventfd_(0, 0);
    poll_fd.events |= POLLIN;
    exit_fd_ = poll_fd.fd;
  } else {
    snprintf(node_path, sizeof(node_path), "%s%d/%s", fb_path_, fb_num_,
             map_event_to_node_[event_data->event_type]);
    poll_fd.fd = Sys::open_(node_path, O_RDONLY);
    poll_fd.events |= POLLPRI | POLLERR;
  }

  if (poll_fd.fd < 0) {
    DLOGW("open failed for display=%d event=%s, error=%s", fb_num_,
          map_event_to_node_[event_data->event_type], strerror(errno));
    return poll_fd;
  }

  // Read once on all fds to clear data on all fds.
  Sys::pread_(poll_fd.fd, data , kMaxStringLength, 0);

  return poll_fd;
}

DisplayError HWEvents::SetEventParser(HWEvent event_type, HWEventData *event_data) {
  DisplayError error = kErrorNone;
  switch (event_type) {
    case HWEvent::VSYNC:
      event_data->event_parser = &HWEvents::HandleVSync;
      break;
    case HWEvent::IDLE_NOTIFY:
      event_data->event_parser = &HWEvents::HandleIdleTimeout;
      break;
    case HWEvent::EXIT:
      event_data->event_parser = &HWEvents::HandleThreadExit;
      break;
    case HWEvent::SHOW_BLANK_EVENT:
      event_data->event_parser = &HWEvents::HandleBlank;
      break;
    case HWEvent::THERMAL_LEVEL:
      event_data->event_parser = &HWEvents::HandleThermal;
      break;
    case HWEvent::IDLE_POWER_COLLAPSE:
      event_data->event_parser = &HWEvents::HandleIdlePowerCollapse;
      break;
    default:
      error = kErrorParameters;
      break;
  }

  return error;
}

void HWEvents::PopulateHWEventData() {
  for (uint32_t i = 0; i < event_list_.size(); i++) {
    HWEventData event_data;
    event_data.event_type = event_list_[i];
    SetEventParser(event_list_[i], &event_data);
    poll_fds_[i] = InitializePollFd(&event_data);
    event_data_list_.push_back(event_data);
  }
}

DisplayError HWEvents::Init(int fb_num, HWEventHandler *event_handler,
                            const vector<HWEvent> &event_list) {
  if (!event_handler)
    return kErrorParameters;

  event_handler_ = event_handler;
  fb_num_ = fb_num;
  event_list_ = event_list;
  poll_fds_.resize(event_list_.size());
  event_thread_name_ += " - " + std::to_string(fb_num_);
  map_event_to_node_ = {{HWEvent::VSYNC, "vsync_event"}, {HWEvent::EXIT, "thread_exit"},
    {HWEvent::IDLE_NOTIFY, "idle_notify"}, {HWEvent::SHOW_BLANK_EVENT, "show_blank_event"},
    {HWEvent::THERMAL_LEVEL, "msm_fb_thermal_level"}, {HWEvent::IDLE_POWER_COLLAPSE, "idle_power_collapse"}};

  PopulateHWEventData();

  if (pthread_create(&event_thread_, NULL, &DisplayEventThread, this) < 0) {
    DLOGE("Failed to start %s, error = %s", event_thread_name_.c_str());
    return kErrorResources;
  }

  return kErrorNone;
}

DisplayError HWEvents::Deinit() {
  exit_threads_ = true;
  Sys::pthread_cancel_(event_thread_);

  uint64_t exit_value = 1;
  ssize_t write_size = Sys::write_(exit_fd_, &exit_value, sizeof(uint64_t));
  if (write_size != sizeof(uint64_t))
    DLOGW("Error triggering exit_fd_ (%d). write size = %d, error = %s", exit_fd_, write_size,
          strerror(errno));

  pthread_join(event_thread_, NULL);

  for (uint32_t i = 0; i < event_list_.size(); i++) {
    Sys::close_(poll_fds_[i].fd);
    poll_fds_[i].fd = -1;
  }

  return kErrorNone;
}

void* HWEvents::DisplayEventThread(void *context) {
  if (context) {
    return reinterpret_cast<HWEvents *>(context)->DisplayEventHandler();
  }

  return NULL;
}

void* HWEvents::DisplayEventHandler() {
  char data[kMaxStringLength] = {0};

  prctl(PR_SET_NAME, event_thread_name_.c_str(), 0, 0, 0);
  setpriority(PRIO_PROCESS, 0, kThreadPriorityUrgent);

  while (!exit_threads_) {
    int error = Sys::poll_(poll_fds_.data(), UINT32(event_list_.size()), -1);

    if (error <= 0) {
      DLOGW("poll failed. error = %s", strerror(errno));
      continue;
    }

    for (uint32_t event = 0; event < event_list_.size(); event++) {
      pollfd &poll_fd = poll_fds_[event];

      if (event_list_.at(event) == HWEvent::EXIT) {
        if ((poll_fd.revents & POLLIN) && (Sys::read_(poll_fd.fd, data, kMaxStringLength) > 0)) {
          (this->*(event_data_list_[event]).event_parser)(data);
        }
      } else {
        if ((poll_fd.revents & POLLPRI) &&
                (Sys::pread_(poll_fd.fd, data, kMaxStringLength, 0) > 0)) {
          (this->*(event_data_list_[event]).event_parser)(data);
        }
      }
    }
  }

  pthread_exit(0);

  return NULL;
}

void HWEvents::HandleVSync(char *data) {
  int64_t timestamp = 0;
  if (!strncmp(data, "VSYNC=", strlen("VSYNC="))) {
    timestamp = strtoll(data + strlen("VSYNC="), NULL, 0);
  }

  event_handler_->VSync(timestamp);
}

void HWEvents::HandleIdleTimeout(char *data) {
  event_handler_->IdleTimeout();
}

void HWEvents::HandleThermal(char *data) {
  int64_t thermal_level = 0;
  if (!strncmp(data, "thermal_level=", strlen("thermal_level="))) {
    thermal_level = strtoll(data + strlen("thermal_level="), NULL, 0);
  }

  DLOGI("Received thermal notification with thermal level = %d", thermal_level);

  event_handler_->ThermalEvent(thermal_level);
}

void HWEvents::HandleIdlePowerCollapse(char *data) {
  event_handler_->IdlePowerCollapse();
}

}  // namespace sdm
