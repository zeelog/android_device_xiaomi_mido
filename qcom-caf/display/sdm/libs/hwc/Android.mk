LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../../common.mk
ifeq ($(use_hwc2),false)

LOCAL_MODULE                  := hwcomposer.$(TARGET_BOARD_PLATFORM)
LOCAL_VENDOR_MODULE           := true
LOCAL_MODULE_RELATIVE_PATH    := hw
LOCAL_MODULE_TAGS             := optional
LOCAL_C_INCLUDES              := $(common_includes)
LOCAL_HEADER_LIBRARIES        := display_headers

LOCAL_CFLAGS                  := $(common_flags) -Wno-missing-field-initializers -Wno-unused-parameter \
                                 -fcolor-diagnostics -Wno-sign-conversion -DLOG_TAG=\"SDM\"
LOCAL_CLANG                   := true

LOCAL_SHARED_LIBRARIES        := libsdmcore libqservice libbinder libhardware libhardware_legacy \
                                 libutils libcutils libsync libmemalloc libqdutils libdl \
                                 libpowermanager libsdmutils libgpu_tonemapper  libc++ liblog \
                                 libdrmutils libui libbfqio

LOCAL_SRC_FILES               := hwc_session.cpp \
                                 hwc_display.cpp \
                                 hwc_display_null.cpp \
                                 hwc_display_primary.cpp \
                                 hwc_display_external.cpp \
                                 hwc_display_virtual.cpp \
                                 hwc_debugger.cpp \
                                 hwc_buffer_allocator.cpp \
                                 hwc_buffer_sync_handler.cpp \
                                 hwc_color_manager.cpp \
                                 blit_engine_c2d.cpp \
                                 cpuhint.cpp \
                                 hwc_tonemapper.cpp \
                                 hwc_socket_handler.cpp \
                                 hwc_display_external_test.cpp

include $(BUILD_SHARED_LIBRARY)
endif
