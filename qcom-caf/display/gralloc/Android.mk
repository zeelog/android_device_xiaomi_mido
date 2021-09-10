# Gralloc module
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../common.mk
include $(LIBION_HEADER_PATH_WRAPPER)

LOCAL_MODULE                  := gralloc.$(TARGET_BOARD_PLATFORM)
LOCAL_VENDOR_MODULE           := true
LOCAL_MODULE_RELATIVE_PATH    := hw
LOCAL_MODULE_TAGS             := optional
LOCAL_C_INCLUDES              := $(common_includes)
LOCAL_CFLAGS                  := $(common_flags) -DLOG_TAG=\"qdgralloc\" -Wall -Werror -Wno-enum-enum-conversion
LOCAL_SHARED_LIBRARIES        := $(common_libs) libqdMetaData libsync libgrallocutils \
                                 android.hardware.graphics.common@1.1
ifeq ($(TARGET_KERNEL_VERSION), 4.14)
  ifneq ($(LIBION_HEADER_PATH_WRAPPER), )
    include $(LIBION_HEADER_PATH_WRAPPER)
    LOCAL_C_INCLUDES += $(LIBION_HEADER_PATHS)
  else
    LOCAL_C_INCLUDES += system/core/libion/include
    LOCAL_C_INCLUDES += system/core/libion/kernel-headers/
  endif
LOCAL_C_INCLUDES              += external/libcxx/include \
                                 $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_SHARED_LIBRARIES        += libion
endif
ifeq ($(TARGET_KERNEL_VERSION), 4.19)
LOCAL_C_INCLUDES              += external/libcxx/include \
                                 $(LIBION_HEADER_PATHS) \
                                 $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_SHARED_LIBRARIES        += libion
endif
LOCAL_HEADER_LIBRARIES        := display_headers
ifneq ($(TARGET_KERNEL_VERSION), $(filter $(TARGET_KERNEL_VERSION),4.14 4.19))
LOCAL_CFLAGS                  += -isystem  $(kernel_includes)
endif
LOCAL_CLANG                   := true
LOCAL_ADDITIONAL_DEPENDENCIES := $(common_deps) $(kernel_deps)
LOCAL_SRC_FILES               := gr_ion_alloc.cpp \
                                 gr_allocator.cpp \
                                 gr_buf_mgr.cpp \
                                 gr_device_impl.cpp

ifeq ($(call is-board-platform-in-list, msm8909), true)
    LOCAL_CFLAGS += -DUSE_SECURE_HEAP
endif

include $(BUILD_SHARED_LIBRARY)

#libgrallocutils
include $(CLEAR_VARS)
LOCAL_MODULE                  := libgrallocutils
LOCAL_VENDOR_MODULE           := true
LOCAL_MODULE_TAGS             := optional
LOCAL_C_INCLUDES              := $(common_includes) $(kernel_includes)
ifeq ($(TARGET_KERNEL_VERSION), 4.14)
  ifneq ($(LIBION_HEADER_PATH_WRAPPER), )
    include $(LIBION_HEADER_PATH_WRAPPER)
    LOCAL_C_INCLUDES += $(LIBION_HEADER_PATHS)
  else
    LOCAL_C_INCLUDES += system/core/libion/include
    LOCAL_C_INCLUDES += system/core/libion/kernel-headers/
  endif
endif
ifeq ($(TARGET_KERNEL_VERSION), 4.19)
LOCAL_C_INCLUDES              += $(LIBION_HEADER_PATHS)
endif
LOCAL_HEADER_LIBRARIES        := display_headers
LOCAL_SHARED_LIBRARIES        := $(common_libs) libqdMetaData libdl android.hardware.graphics.common@1.1
LOCAL_CFLAGS                  := $(common_flags) -DLOG_TAG=\"grallocutils\" -Wno-sign-conversion -Wno-enum-enum-conversion
LOCAL_ADDITIONAL_DEPENDENCIES := $(common_deps) $(kernel_deps)
LOCAL_SRC_FILES               := gr_utils.cpp gr_adreno_info.cpp
include $(BUILD_SHARED_LIBRARY)
