PRODUCT_PACKAGES += libdisplayconfig.system \
                    libqdMetaData.system \
                    libgralloc.system.qti \
                    libdrm \
                    liblayerext.qti \
                    libsmomoconfig.qti \
                    libcomposerextn.qti \
                    libdisplayconfig.system.qti

SOONG_CONFIG_NAMESPACES += qtidisplaycommonsys
SOONG_CONFIG_NAMESPACES += qtiunifeddraw
# Soong Keys
SOONG_CONFIG_qtidisplaycommonsys := displayconfig_enabled
SOONG_CONFIG_qtiunifeddraw := qtiunifeddraw_enabled
# Soong Values
SOONG_CONFIG_qtidisplaycommonsys_displayconfig_enabled := false
SOONG_CONFIG_qtiunifeddraw_qtiunifeddraw_enabled := true

ifeq ($(call is-vendor-board-platform,QCOM),true)
    SOONG_CONFIG_qtidisplaycommonsys_displayconfig_enabled := true
endif
