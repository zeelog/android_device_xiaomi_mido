VNDK_SP_LIBRARIES := \
    android.hardware.graphics.allocator@2.0 \
    android.hardware.graphics.common@1.0 \
    android.hardware.graphics.mapper@2.0 \
    android.hardware.renderscript@1.0 \
    android.hidl.memory@1.0 \
    libRSCpuRef \
    libRSDriver \
    libRS_internal \
    libbacktrace \
    libbase \
    libbcinfo \
    libblas \
    libc++ \
    libcompiler_rt \
    libcutils \
    libft2 \
    libhardware \
    libhidlbase \
    libhidlmemory \
    libhidltransport \
    libhwbinder \
    libion \
    liblzma \
    libpng \
    libunwind \
    libutils \
    libz

EXTRA_VENDOR_LIBRARIES := \
    android.hidl.base@1.0 \
    android.hidl.manager@1.0 \
    vendor.display.color@1.0 \
    vendor.display.config@1.0


#-------------------------------------------------------------------------------
# VNDK Modules
#-------------------------------------------------------------------------------
LOCAL_PATH := $(call my-dir)

define define-vndk-lib
include $$(CLEAR_VARS)
LOCAL_MODULE := $1.$2
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_PREBUILT_MODULE_FILE := $$(TARGET_OUT_INTERMEDIATE_LIBRARIES)/$1.so
LOCAL_STRIP_MODULE := false
LOCAL_MULTILIB := first
LOCAL_MODULE_TAGS := optional
LOCAL_INSTALLED_MODULE_STEM := $1.so
LOCAL_MODULE_SUFFIX := .so
LOCAL_MODULE_RELATIVE_PATH := $3
LOCAL_VENDOR_MODULE := $4
include $$(BUILD_PREBUILT)

ifneq ($$(TARGET_2ND_ARCH),)
ifneq ($$(TARGET_TRANSLATE_2ND_ARCH),true)
include $$(CLEAR_VARS)
LOCAL_MODULE := $1.$2
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_PREBUILT_MODULE_FILE := $$($$(TARGET_2ND_ARCH_VAR_PREFIX)TARGET_OUT_INTERMEDIATE_LIBRARIES)/$1.so
LOCAL_STRIP_MODULE := false
LOCAL_MULTILIB := 32
LOCAL_MODULE_TAGS := optional
LOCAL_INSTALLED_MODULE_STEM := $1.so
LOCAL_MODULE_SUFFIX := .so
LOCAL_MODULE_RELATIVE_PATH := $3
LOCAL_VENDOR_MODULE := $4
include $$(BUILD_PREBUILT)
endif  # TARGET_TRANSLATE_2ND_ARCH is not true
endif  # TARGET_2ND_ARCH is not empty
endef

$(foreach lib,$(VNDK_SP_LIBRARIES),\
    $(eval $(call define-vndk-lib,$(lib),vndk-sp-gen,vndk-sp-28,)))
$(foreach lib,$(VNDK_SP_EXT_LIBRARIES),\
    $(eval $(call define-vndk-lib,$(lib),vndk-sp-ext-gen,vndk-sp,true)))
$(foreach lib,$(EXTRA_VENDOR_LIBRARIES),\
    $(eval $(call define-vndk-lib,$(lib),vndk-ext-gen,,true)))


#-------------------------------------------------------------------------------
# Phony Package
#-------------------------------------------------------------------------------

include $(CLEAR_VARS)
LOCAL_MODULE := vndk-sp
LOCAL_MODULE_TAGS := optional
LOCAL_REQUIRED_MODULES := \
    $(addsuffix .vndk-sp-gen,$(VNDK_SP_LIBRARIES)) \
    $(addsuffix .vndk-sp-ext-gen,$(VNDK_SP_EXT_LIBRARIES)) \
    $(addsuffix .vndk-ext-gen,$(EXTRA_VENDOR_LIBRARIES))
include $(BUILD_PHONY_PACKAGE)

