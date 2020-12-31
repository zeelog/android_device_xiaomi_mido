#
# Copyright (C) 2020 AIMROM
# Copyright (C) 2020 KudProject Development
#
# SPDX-License-Identifier: Apache-2.0
#
# Charger
PRODUCT_PRODUCT_PROPERTIES += \
    ro.charger.disable_init_blank=true \
    ro.charger.enable_suspend=true

# Default to BFQ I/O scheduler
PRODUCT_PRODUCT_PROPERTIES += \
    persist.sys.io.scheduler=bfq
