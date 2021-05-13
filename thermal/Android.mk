# Copyright (C) 2015 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := thermal.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PROPRIETARY_MODULE := true

LOCAL_SRC_FILES := thermal.c
LOCAL_SRC_FILES += thermal_common.c

ifeq ($(call is-board-platform-in-list,msm8998), true)
LOCAL_SRC_FILES += thermal-8998.c
else ifeq ($(call is-board-platform-in-list,sdm845), true)
LOCAL_SRC_FILES += thermal-845.c
else
LOCAL_SRC_FILES += thermal_target.c
endif

LOCAL_HEADER_LIBRARIES := libutils_headers libhardware_headers
LOCAL_SHARED_LIBRARIES := liblog libcutils
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS := -Wno-unused-parameter

include $(BUILD_SHARED_LIBRARY)
