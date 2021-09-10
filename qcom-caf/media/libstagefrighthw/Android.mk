#
# Copyright (C) 2009 The Android Open Source Project
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
#

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    QComOMXPlugin.cpp                      \

LOCAL_CFLAGS := $(PV_CFLAGS_MINUS_VISIBILITY)

ifeq ($(PLATFORM_SDK_VERSION), 18)  #JB_MR2
LOCAL_CFLAGS += -DANDROID_JELLYBEAN_MR2=1
endif

ifeq ($(TARGET_USES_MEDIA_EXTENSIONS),true)
LOCAL_CFLAGS += -DUSE_NATIVE_HANDLE_SOURCE
endif

LOCAL_HEADER_LIBRARIES := \
        media_plugin_headers \
        libcutils_headers \
        libutils_headers

LOCAL_SHARED_LIBRARIES :=       \
        libutils                \
        libcutils               \
        libdl                   \

LOCAL_MODULE := libstagefrighthw

LOCAL_VENDOR_MODULE := true

include $(BUILD_SHARED_LIBRARY)

