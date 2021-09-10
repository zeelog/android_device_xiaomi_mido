PRODUCT_PACKAGES += libdisplayconfig \
                    libqdMetaData \
                    libqdMetaData.system \
                    libgralloc.qti \
                    libdrm \
                    vendor.display.config@1.0 \
                    vendor.display.config@1.1 \
                    vendor.display.config@1.2 \
                    vendor.display.config@1.3 \
                    vendor.display.config@1.4 \
                    vendor.display.config@1.5 \
                    libsmomo.qti \
                    liblayerext.qti \
                    libsmomoconfig.qti \
                    libcomposerextn.qti \
                    libdisplayconfig.qti

SOONG_CONFIG_NAMESPACES += qtidisplaycommonsys
# Soong Keys
SOONG_CONFIG_qtidisplaycommonsys := displayconfig_enabled
# Soong Values
SOONG_CONFIG_qtidisplaycommonsys_displayconfig_enabled := false

ifeq ($(call is-vendor-board-platform,QCOM),true)
    SOONG_CONFIG_qtidisplaycommonsys_displayconfig_enabled := true
endif
