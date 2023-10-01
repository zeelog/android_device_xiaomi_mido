/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *	* Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 *	* Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials provided
 *	  with the distribution.
 *	* Neither the name of The Linux Foundation nor the names of its
 *	  contributors may be used to endorse or promote products derived
 *	  from this software without specific prior written permission.
 *
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


#ifndef THERMAL_THERMAL_MONITOR_NETLINK_H__
#define THERMAL_THERMAL_MONITOR_NETLINK_H__

#include <thread>
#include <netlink/genl/genl.h>
#include <netlink/genl/mngt.h>
#include <netlink/genl/ctrl.h>
#include <netlink/netlink.h>
#include <android/hardware/thermal/2.0/IThermal.h>

namespace android {
namespace hardware {
namespace thermal {
namespace V2_0 {
namespace implementation {

using eventMonitorCB = std::function<void(int, int)>;
using eventCreateMonitorCB = std::function<void(int, const char *)>;

class ThermalMonitor {
	public:
		ThermalMonitor(const eventMonitorCB &inp_event_cb,
			const eventMonitorCB &inp_sample_cb,
			const eventCreateMonitorCB &inp_event_create_cb);
		~ThermalMonitor();

		void parse_and_notify(char *inp_buf, ssize_t len);
		bool stopPolling()
		{
			return monitor_shutdown;
		}
		void start();
		int family_msg_cb(struct nl_msg *msg, void *data);
		int event_parse(struct nl_msg *n, void *data);
		int sample_parse(struct nl_msg *n, void *data);
	private:
		std::thread event_th, sample_th;
		struct nl_sock *event_soc, *sample_soc;
		int event_group, sample_group;
		bool monitor_shutdown;
		eventMonitorCB event_cb, sample_cb;
		eventCreateMonitorCB event_create_cb;

		int fetch_group_id();
		int send_nl_msg(struct nl_msg *msg);
};

}  // namespace implementation
}  // namespace V2_0
}  // namespace thermal
}  // namespace hardware
}  // namespace android

#endif  // THERMAL_THERMAL_MONITOR_NETLINK_H__
