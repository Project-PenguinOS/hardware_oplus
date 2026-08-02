#
# Copyright (C) 2024 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

PRODUCT_PACKAGES += oplus-fwk
PRODUCT_BOOT_JARS += oplus-fwk

# Declares the vendor permissions that stock ships in oplus-framework-res.apk,
# which ported oplus apps request and guard their components with.
PRODUCT_PACKAGES += OplusPermissions
