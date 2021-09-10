# Copyright 2013 The Android Open Source Project
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

ifneq ($(AUDIO_USE_STUB_HAL), true)
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    offload_visualizer.c

LOCAL_CFLAGS+= -O2 -fvisibility=hidden

LOCAL_CFLAGS += \
    -Wall \
    -Werror \
    -Wno-unused-variable \
    -Wno-unused-parameter \
    -Wno-gnu-designator \
    -Wno-unused-value \
    -Wno-typedef-redefinition

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_GCOV)),true)
LOCAL_CFLAGS += --coverage -fprofile-arcs -ftest-coverage
LOCAL_CPPFLAGS += --coverage -fprofile-arcs -ftest-coverage
LOCAL_STATIC_LIBRARIES += libprofile_rt
endif

LOCAL_HEADER_LIBRARIES := libsystem_headers \
                          libhardware_headers
LOCAL_SHARED_LIBRARIES := \
    libcutils \
    liblog \
    libdl \
    libtinyalsa

LOCAL_MODULE_RELATIVE_PATH := soundfx
LOCAL_MODULE:= libqcomvisualizer
LOCAL_VENDOR_MODULE := true

LOCAL_C_INCLUDES := \
    external/tinyalsa/include \
    $(call include-path-for, audio-effects)

LOCAL_CFLAGS += -Wno-unused-variable
LOCAL_CFLAGS += -Wno-sign-compare
LOCAL_CFLAGS += -Wno-unused-parameter
LOCAL_CFLAGS += -Wno-unused-label
LOCAL_CFLAGS += -Wno-gnu-designator
LOCAL_CFLAGS += -Wno-typedef-redefinition
LOCAL_CFLAGS += -Wno-shorten-64-to-32
LOCAL_CFLAGS += -Wno-tautological-compare
LOCAL_CFLAGS += -Wno-unused-function
LOCAL_CFLAGS += -Wno-unused-local-typedef

ifneq ($(filter kona lahaina holi,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SANITIZE := integer_overflow
endif
include $(BUILD_SHARED_LIBRARY)
endif
