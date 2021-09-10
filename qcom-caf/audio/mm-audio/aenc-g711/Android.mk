ifneq ($(filter arm aarch64 arm64, $(TARGET_ARCH)),)

AENC_G7111_PATH:= $(call my-dir)

include $(AENC_G7111_PATH)/qdsp6/Android.mk

endif
