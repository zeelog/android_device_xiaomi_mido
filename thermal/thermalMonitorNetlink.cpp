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

#include <unistd.h>
#include <linux/thermal.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>

#include "thermalMonitorNetlink.h"

namespace android {
namespace hardware {
namespace thermal {
namespace V2_0 {
namespace implementation {

using pollCB = std::function<bool()>;
using familyCB = std::function<int(struct nl_msg *, void *)>;

void thermal_listen(struct nl_sock *soc, const pollCB &stopCB)
{
	while (!stopCB())
		nl_recvmsgs_default(soc);

	LOG(INFO) << "thermal_listen_event Exit" << std::endl;
	return;
}

int thermal_event_cb(struct nl_msg *n, void *data)
{
	ThermalMonitor *t = (ThermalMonitor *)data;
	return t->event_parse(n, NULL);
}

int thermal_sample_cb(struct nl_msg *n, void *data)
{
	ThermalMonitor *t = (ThermalMonitor *)data;
	return t->sample_parse(n, NULL);
}

int thermal_family_cb(struct nl_msg *n, void *data)
{
	ThermalMonitor *t = (ThermalMonitor *)data;
	return t->family_msg_cb(n, NULL);
}

ThermalMonitor::ThermalMonitor(const eventMonitorCB &inp_event_cb,
				const eventMonitorCB &inp_sample_cb,
				const eventCreateMonitorCB &inp_event_create_cb):
	event_group(-1),
	sample_group(-1),
	event_cb(inp_event_cb),
	sample_cb(inp_sample_cb),
	event_create_cb(inp_event_create_cb)
{
	monitor_shutdown = false;
}

ThermalMonitor::~ThermalMonitor()
{
	monitor_shutdown = true;
	event_th.join();
	sample_th.join();
	if (sample_soc)
		nl_socket_free(sample_soc);
	if (event_soc)
		nl_socket_free(event_soc);
}

int ThermalMonitor::event_parse(struct nl_msg *n, void *data)
{
	struct nlmsghdr *nl_hdr = nlmsg_hdr(n);
	struct genlmsghdr *hdr = genlmsg_hdr(nl_hdr);
	struct nlattr *attrs[THERMAL_GENL_ATTR_MAX + 1];
	int tzn = -1, trip = -1;
	const char *tz_name = "";

	genlmsg_parse(nl_hdr, 0, attrs, THERMAL_GENL_ATTR_MAX, NULL);

	switch (hdr->cmd) {
	case THERMAL_GENL_EVENT_TZ_TRIP_UP:
	case THERMAL_GENL_EVENT_TZ_TRIP_DOWN:
		if (attrs[THERMAL_GENL_ATTR_TZ_ID])
			tzn = nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_ID]);

		if (attrs[THERMAL_GENL_ATTR_TZ_TRIP_ID])
			trip = nla_get_u32(
					attrs[THERMAL_GENL_ATTR_TZ_TRIP_ID]);
		LOG(DEBUG) << "thermal_nl_event: TZ:" << tzn << " Trip:"
		       << trip << "event:" << (int)hdr->cmd << std::endl;
		event_cb(tzn, trip);
		break;
	case THERMAL_GENL_EVENT_TZ_CREATE:
		if (attrs[THERMAL_GENL_ATTR_TZ_ID])
			tzn = nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_ID]);
		if (attrs[THERMAL_GENL_ATTR_TZ_NAME])
			tz_name = nla_get_string(attrs[THERMAL_GENL_ATTR_TZ_NAME]);
		LOG(DEBUG) << "thermal_nl_event: TZ_CREATE: TZ:" << tzn << " TZ_NAME:"
		       << tz_name << "event:" << (int)hdr->cmd << std::endl;
		event_create_cb(tzn, tz_name);
		break;
	}

	return 0;

}

int ThermalMonitor::sample_parse(struct nl_msg *n, void *data)
{
	struct nlmsghdr *nl_hdr = nlmsg_hdr(n);
	struct genlmsghdr *hdr = genlmsg_hdr(nl_hdr);
	struct nlattr *attrs[THERMAL_GENL_ATTR_MAX + 1];
	int tzn = -1, temp = -1;

	genlmsg_parse(nl_hdr, 0, attrs, THERMAL_GENL_ATTR_MAX, NULL);

	switch (hdr->cmd) {
	case THERMAL_GENL_SAMPLING_TEMP:
		if (attrs[THERMAL_GENL_ATTR_TZ_ID])
			tzn = nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_ID]);

		if (attrs[THERMAL_GENL_ATTR_TZ_TEMP])
			temp = nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_TEMP]);

		LOG(INFO) << "thermal_sample_event: TZ:" << tzn << " temp:"
			<< temp << std::endl;
		sample_cb(tzn, temp);
		break;
	}

	return 0;

}

int ThermalMonitor::family_msg_cb(struct nl_msg *msg, void *data)
{
	struct nlattr *tb[CTRL_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = genlmsg_hdr(nlmsg_hdr(msg));
	struct nlattr *mc_group;
	int rem_mcgrp;

	nla_parse(tb, CTRL_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (!tb[CTRL_ATTR_MCAST_GROUPS]) {
		LOG(ERROR) << "Multicast group not available\n";
		return -1;
	}

	nla_for_each_nested(mc_group, tb[CTRL_ATTR_MCAST_GROUPS], rem_mcgrp) {

		struct nlattr *nl_group[CTRL_ATTR_MCAST_GRP_MAX + 1];

		nla_parse(nl_group, CTRL_ATTR_MCAST_GRP_MAX,
			  (struct nlattr *)nla_data(mc_group),
			  nla_len(mc_group), NULL);

		if (!nl_group[CTRL_ATTR_MCAST_GRP_NAME] ||
		    !nl_group[CTRL_ATTR_MCAST_GRP_ID])
			continue;
		std::string family((char *)nla_data(
					nl_group[CTRL_ATTR_MCAST_GRP_NAME]));
		LOG(DEBUG) << "Family:" << family << std::endl;
		if (family == THERMAL_GENL_EVENT_GROUP_NAME)
			event_group = nla_get_u32(
					nl_group[CTRL_ATTR_MCAST_GRP_ID]);

		if (family == THERMAL_GENL_SAMPLING_GROUP_NAME)
			sample_group = nla_get_u32(
					nl_group[CTRL_ATTR_MCAST_GRP_ID]);
	}

	return 0;
}

int ThermalMonitor::send_nl_msg(struct nl_msg *msg)
{
	int ret = 0;

	ret = nl_send_auto(event_soc, msg);
	if (ret < 0) {
		LOG(ERROR) << "Error sending NL message\n";
		return ret;
	}
	nl_socket_disable_seq_check(event_soc);
	nl_socket_modify_cb(event_soc, NL_CB_VALID, NL_CB_CUSTOM,
			thermal_family_cb, this);
	ret = nl_recvmsgs_default(event_soc);

	return ret;
}

int ThermalMonitor::fetch_group_id(void)
{
	struct nl_msg *msg;
	int ctrlid, ret = 0;

	msg = nlmsg_alloc();
	if (!msg)
		return -1;

	ctrlid = genl_ctrl_resolve(event_soc, "nlctrl");
	genlmsg_put(msg, 0, 0, ctrlid, 0, 0, CTRL_CMD_GETFAMILY, 0);

	nla_put_string(msg, CTRL_ATTR_FAMILY_NAME, THERMAL_GENL_FAMILY_NAME);
	send_nl_msg(msg);

	nlmsg_free(msg);

	if (event_group != -1 && sample_group != -1) {
		LOG(DEBUG) << "Event: " << event_group <<
			" Sample:" << sample_group << std::endl;
		ret = nl_socket_add_membership(event_soc, event_group);
		if (ret) {
			LOG(ERROR) << "Event Socket membership add error\n";
			return ret;
		}

		ret = nl_socket_add_membership(sample_soc, sample_group);
		if (ret) {
			LOG(ERROR) << "sample Socket membership add error\n";
			return ret;
		}
	}
	return 0;
}

void ThermalMonitor::start()
{
	struct nl_msg *msg;

	event_soc = nl_socket_alloc();
	if (!event_soc) {
		LOG(ERROR) << "Event socket alloc failed\n";
		return;
	}

	if (genl_connect(event_soc)) {
		LOG(ERROR) << "Event socket connect failed\n";
		nl_socket_free(event_soc);
		event_soc = nullptr;
		return;
	}
	sample_soc = nl_socket_alloc();
	if (!sample_soc) {
		LOG(ERROR) << "Sample socket alloc failed\n";
		nl_socket_free(event_soc);
		event_soc = nullptr;
		return;
	}

	if (genl_connect(sample_soc)) {
		LOG(ERROR) << "Sample socket connect failed\n";
		nl_socket_free(sample_soc);
		nl_socket_free(event_soc);
		event_soc = nullptr;
		sample_soc = nullptr;
		return;
	}
	if (fetch_group_id())
		return;
	nl_socket_disable_seq_check(sample_soc);
	nl_socket_modify_cb(sample_soc, NL_CB_VALID, NL_CB_CUSTOM,
			thermal_sample_cb, this);
	nl_socket_disable_seq_check(event_soc);
	nl_socket_modify_cb(event_soc, NL_CB_VALID, NL_CB_CUSTOM,
			thermal_event_cb, this);
	event_th = std::thread(thermal_listen, event_soc,
		std::bind(&ThermalMonitor::stopPolling, this));

	sample_th = std::thread(thermal_listen, sample_soc,
		std::bind(&ThermalMonitor::stopPolling, this));
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace thermal
}  // namespace hardware
}  // namespace android
