/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
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

#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <linux/types.h>
#include <linux/netlink.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>

#include "thermalMonitor.h"

#define UEVENT_BUF 1024

#define HYST_FMT "change@/devices/virtual/thermal/thermal_zone%d\n\
	ACTION=change\n\
	DEVPATH=/devices/virtual/thermal/thermal_zone%d\n\
	SUBSYSTEM=thermal\n\
	NAME=%s\n\
	TEMP=%d\n\
	HYST=%d\n\
	EVENT=%d\n"\

#define TRIP_FMT "change@/devices/virtual/thermal/thermal_zone%d\n\
	ACTION=change\n\
	DEVPATH=/devices/virtual/thermal/thermal_zone%d\n\
	SUBSYSTEM=thermal\n\
	NAME=%s\n\
	TEMP=%d\n\
	TRIP=%d\n\
	EVENT=%d\n"\

namespace android {
namespace hardware {
namespace thermal {
namespace V2_0 {
namespace implementation {

using parseCB = std::function<void(char *inp_buf, ssize_t len)>;
using pollCB = std::function<bool()>;

void thermal_monitor_uevent(const parseCB &parse_cb, const pollCB &stopPollCB)
{
	struct pollfd pfd;
	char buf[UEVENT_BUF] = {0};
	int sz = 64*1024;
	struct sockaddr_nl nls;

	memset(&nls, 0, sizeof(nls));
	nls.nl_family = AF_NETLINK;
	nls.nl_pid = getpid();
	nls.nl_groups = 0xffffffff;

	pfd.events = POLLIN;
	pfd.fd = socket(PF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC,
			NETLINK_KOBJECT_UEVENT);
	if (pfd.fd < 0) {
		LOG(ERROR) << "socket creation error:" << errno << std::endl;
		return;
	}
	LOG(DEBUG) << "socket creation success" << std::endl;

	setsockopt(pfd.fd, SOL_SOCKET, SO_RCVBUF, &sz, sizeof(sz));
	if (bind(pfd.fd, (struct sockaddr *)&nls, sizeof(nls)) < 0) {
		close(pfd.fd);
		LOG(ERROR) << "socket bind failed:" << errno << std::endl;
		return;
	}
	LOG(DEBUG) << "Listening for uevent" << std::endl;

	while (!stopPollCB()) {
		ssize_t len;
		int err;

		err = poll(&pfd, 1, -1);
		if (err == -1) {
			LOG(ERROR) << "Error in uevent poll.";
			break;
		}
		if (stopPollCB()) {
			LOG(INFO) << "Exiting uevent monitor" << std::endl;
			return;
		}
		len = recv(pfd.fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
		if (len == -1) {
			LOG(ERROR) << "uevent read failed:" << errno << std::endl;
			continue;
		}
		buf[len] = '\0';

		parse_cb(buf, len);
	}

	return;
}

ThermalMonitor::ThermalMonitor(const ueventMonitorCB &inp_cb):
	cb(inp_cb)
{
	monitor_shutdown = false;
}

ThermalMonitor::~ThermalMonitor()
{
	monitor_shutdown = true;
	th.join();
}

void ThermalMonitor::start()
{
	th = std::thread(thermal_monitor_uevent,
		std::bind(&ThermalMonitor::parse_and_notify, this,
			std::placeholders::_1, std::placeholders::_2),
		std::bind(&ThermalMonitor::stopPolling, this));
}

void ThermalMonitor::parse_and_notify(char *inp_buf, ssize_t len)
{
	int zone_num, temp, trip, ret = 0, event;
	ssize_t i = 0;
	char sensor_name[30] = "", buf[UEVENT_BUF] = {0};

	LOG(DEBUG) << "monitor received thermal uevent: " << inp_buf
		<< std::endl;

	while (i < len) {
		if (i >= UEVENT_BUF)
			return;
		ret = snprintf(buf + i, UEVENT_BUF - i, "%s ", inp_buf + i);
		if (ret == (strlen(inp_buf + i) + 1))
			i += ret;
		else
			return;
	}

	if (!strstr(buf, "SUBSYSTEM=thermal"))
		return;

	if (strstr(buf, "TRIP=")) {
		ret = sscanf(buf, TRIP_FMT, &zone_num, &zone_num, sensor_name,
			&temp, &trip, &event);
		LOG(DEBUG) << "zone:" << zone_num << " sensor:" << sensor_name
		       <<" temp:" << temp << " trip:" << trip << " event:" <<
		       event << std::endl;
	} else {
		ret = sscanf(buf, HYST_FMT, &zone_num, &zone_num, sensor_name,
			&temp, &trip, &event);
		LOG(DEBUG) << "zone:" << zone_num << " sensor:" << sensor_name
		       <<" temp:" << temp << " trip:" << trip << " event:" <<
		       event << std::endl;
	}
	if (ret <= 0 || ret == EOF) {
		LOG(ERROR) << "read error:" << ret <<". buf:" << buf << std::endl;
		return;
	}
	cb(sensor_name, temp);
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace thermal
}  // namespace hardware
}  // namespace android
