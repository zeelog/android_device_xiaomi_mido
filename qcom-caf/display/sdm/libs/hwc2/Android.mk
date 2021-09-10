LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../../common.mk

ifeq ($(use_hwc2),true)

LOCAL_MODULE                  := hwcomposer.$(TARGET_BOARD_PLATFORM)
LOCAL_VENDOR_MODULE           := true
LOCAL_MODULE_RELATIVE_PATH    := hw
LOCAL_MODULE_TAGS             := optional
LOCAL_C_INCLUDES              := $(common_includes) \
                                 $(kernel_includes)
LOCAL_HEADER_LIBRARIES        := display_headers
LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

LOCAL_CFLAGS                  := -Wno-missing-field-initializers -Wno-unused-parameter \
                                 -fcolor-diagnostics\
                                 -DLOG_TAG=\"SDM\" $(common_flags) \
                                 -I $(display_top)/sdm/libs/hwc

ifeq ($(TARGET_KERNEL_VERSION),$(filter $(TARGET_KERNEL_VERSION),4.14 4.19))
LOCAL_CFLAGS += -DTARGET_MIN_KERNEL_4_14
endif

ifeq ($(TARGET_EXCLUDES_DISPLAY_PP), true)
LOCAL_CFLAGS += -DEXCLUDE_DISPLAY_PP
endif

ifeq ($(TARGET_EXCLUDES_MULTI_DISPLAY),true)
LOCAL_CFLAGS += -DEXCLUDES_MULTI_DISPLAY
endif

LOCAL_CLANG                   := true

# TODO: Remove libui after addressing gpu_tonemapper issues
LOCAL_SHARED_LIBRARIES        := libsdmcore libqservice libbinder libhardware libhardware_legacy \
                                 libutils libcutils libsync libqdutils libqdMetaData libdl libdrmutils \
                                 libsdmutils libc++ liblog libgrallocutils libdl \
                                 vendor.display.config@2.0 libhidlbase \
                                 libdisplayconfig.qti libui libgpu_tonemapper libbfqio

ifneq ($(TARGET_USES_GRALLOC1), true)
    LOCAL_SHARED_LIBRARIES += libmemalloc
endif

LOCAL_SRC_FILES               := hwc_session.cpp \
                                 hwc_session_services.cpp \
                                 hwc_display.cpp \
                                 hwc_display_primary.cpp \
                                 hwc_display_external.cpp \
                                 hwc_display_virtual.cpp \
                                 hwc_display_dummy.cpp \
                                 ../hwc/hwc_debugger.cpp \
                                 ../hwc/hwc_buffer_sync_handler.cpp \
                                 hwc_color_manager.cpp \
                                 hwc_layers.cpp \
                                 hwc_callbacks.cpp \
                                 ../hwc/cpuhint.cpp \
                                 ../hwc/hwc_socket_handler.cpp \
                                 display_null.cpp \
                                 hwc_tonemapper.cpp \
                                 hwc_display_external_test.cpp

ifneq ($(TARGET_USES_GRALLOC1), true)
    LOCAL_SRC_FILES += ../hwc/hwc_buffer_allocator.cpp
else
    LOCAL_SRC_FILES += hwc_buffer_allocator.cpp
endif

ifeq ($(TARGET_HAS_WIDE_COLOR_DISPLAY), true)
    LOCAL_CFLAGS += -DFEATURE_WIDE_COLOR
endif

include $(BUILD_SHARED_LIBRARY)
endif
