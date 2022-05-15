PRODUCT_PACKAGES += vendor.display.config@1.0 \
                    vendor.display.config@1.1 \
                    vendor.display.config@1.2 \
                    vendor.display.config@1.3 \
                    vendor.display.config@1.4 \
                    vendor.display.config@1.5 \
                    vendor.qti.hardware.display.config-V1-ndk_platform \
                    vendor.qti.hardware.display.config-V2-ndk_platform \
                    vendor.qti.hardware.display.config-V3-ndk_platform \
                    vendor.qti.hardware.display.config-V4-ndk_platform \
                    vendor.qti.hardware.display.config-V5-ndk_platform \
                    vendor.qti.hardware.display.config-V6-ndk_platform

SOONG_CONFIG_NAMESPACES += qtiunifeddraw
# Soong Keys
SOONG_CONFIG_qtiunifeddraw := qtiunifeddraw_enabled
# Soong Values
SOONG_CONFIG_qtiunifeddraw_qtiunifeddraw_enabled := true
