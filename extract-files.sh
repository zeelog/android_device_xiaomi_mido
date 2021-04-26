#!/bin/bash
#
# Copyright (C) 2016 The CyanogenMod Project
# Copyright (C) 2017-2020 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0

if [ "${BASH_SOURCE[0]}" != "${0}" ]; then
    return
fi

set -e

# Required!
DEVICE=mido
VENDOR=xiaomi

# Load extract_utils and do some sanity checks
MY_DIR="${BASH_SOURCE%/*}"
if [[ ! -d "${MY_DIR}" ]]; then MY_DIR="${PWD}"; fi

ANDROID_ROOT="${MY_DIR}/../../.."

HELPER="${ANDROID_ROOT}/tools/extract-utils/extract_utils.sh"
if [ ! -f "$HELPER" ]; then
    echo "Unable to find helper script at $HELPER"
    exit 1
fi
source "${HELPER}"

# Default to sanitizing the vendor folder before extraction
CLEAN_VENDOR=true

KANG=
SECTION=

while [ "${#}" -gt 0 ]; do
    case "${1}" in
        -n | --no-cleanup )
                CLEAN_VENDOR=false
                ;;
        -k | --kang )
                KANG="--kang"
                ;;
        -s | --section )
                SECTION="${2}"; shift
                CLEAN_VENDOR=false
                ;;
        * )
                SRC="${1}"
                ;;
    esac
    shift
done

if [ -z "${SRC}" ]; then
    SRC="adb"
fi

# Initialize the helper
setup_vendor "${DEVICE}" "${VENDOR}" "${ANDROID_ROOT}" false "${CLEAN_VENDOR}"

extract "${MY_DIR}"/proprietary-files.txt "${SRC}" \
        "${KANG}" --section "${SECTION}"

DEVICE_BLOB_ROOT="${ANDROID_ROOT}"/vendor/"${VENDOR}"/"${DEVICE}"/proprietary

# Camera configs
sed -i "s|/system/etc/camera|/vendor/etc/camera|g" "${DEVICE_BLOB_ROOT}"/vendor/lib/libmmcamera2_sensor_modules.so

# Camera socket
sed -i "s|/data/misc/camera/cam_socket|/data/vendor/qcam/cam_socket|g" "${DEVICE_BLOB_ROOT}"/vendor/bin/mm-qcamera-daemon

# Camera data
for CAMERA_LIB in libmmcamera2_cpp_module.so libmmcamera2_dcrf.so libmmcamera2_iface_modules.so libmmcamera2_imglib_modules.so libmmcamera2_mct.so libmmcamera2_pproc_modules.so libmmcamera2_q3a_core.so libmmcamera2_sensor_modules.so libmmcamera2_stats_algorithm.so libmmcamera2_stats_modules.so libmmcamera_dbg.so libmmcamera_imglib.so libmmcamera_pdafcamif.so libmmcamera_pdaf.so libmmcamera_tintless_algo.so libmmcamera_tintless_bg_pca_algo.so libmmcamera_tuning.so; do
    sed -i "s|/data/misc/camera/|/data/vendor/qcam/|g" "${DEVICE_BLOB_ROOT}"/vendor/lib/${CAMERA_LIB}
done

# Camera debug log file
sed -i "s|persist.camera.debug.logfile|persist.vendor.camera.dbglog|g" "${DEVICE_BLOB_ROOT}"/vendor/lib/libmmcamera_dbg.so

# Camera graphicbuffer shim
"${PATCHELF}" --add-needed "libui_shim.so" "${DEVICE_BLOB_ROOT}"/vendor/lib/libmmcamera_ppeiscore.so

# Camera VNDK support
"${PATCHELF}" --remove-needed "libandroid.so" "${DEVICE_BLOB_ROOT}"/vendor/lib/libmmcamera2_stats_modules.so
"${PATCHELF}" --remove-needed "libgui.so" "${DEVICE_BLOB_ROOT}"/vendor/lib/libmmcamera2_stats_modules.so
sed -i "s|libandroid.so|libcamshim.so|g" "${DEVICE_BLOB_ROOT}"/vendor/lib/libmmcamera2_stats_modules.so
"${PATCHELF}" --remove-needed "libgui.so" "${DEVICE_BLOB_ROOT}"/vendor/lib/libmmcamera_ppeiscore.so
"${PATCHELF}" --remove-needed "libandroid.so" "${DEVICE_BLOB_ROOT}"/vendor/lib/libmpbase.so

# Gnss
sed -i -e '$a\\    capabilities NET_BIND_SERVICE' "${DEVICE_BLOB_ROOT}"/vendor/etc/init/android.hardware.gnss@2.1-service-qti.rc

# Goodix
"${PATCHELF}" --remove-needed "libunwind.so" "${DEVICE_BLOB_ROOT}"/vendor/bin/gx_fpd
"${PATCHELF}" --remove-needed "libbacktrace.so" "${DEVICE_BLOB_ROOT}"/vendor/bin/gx_fpd
"${PATCHELF}" --add-needed "libshims_gxfpd.so" "${DEVICE_BLOB_ROOT}"/vendor/bin/gx_fpd
"${PATCHELF}" --add-needed "fakelogprint.so" "${DEVICE_BLOB_ROOT}"/vendor/bin/gx_fpd
"${PATCHELF}" --add-needed "fakelogprint.so" "${DEVICE_BLOB_ROOT}"/vendor/lib64/hw/fingerprint.goodix.so
"${PATCHELF}" --add-needed "fakelogprint.so" "${DEVICE_BLOB_ROOT}"/vendor/lib64/hw/gxfingerprint.default.so

# WFD
"${PATCHELF}" --add-needed "libshim_wfdmmsink.so" "${DEVICE_BLOB_ROOT}"/lib/libwfdmmsink.so

# Wcnss_service - libqmiservices_shim
"${PATCHELF}" --add-needed "libqmiservices_shim.so" "${DEVICE_BLOB_ROOT}"/vendor/bin/wcnss_service
sed -i "s|dms_get_service_object_internal_v01|dms_get_service_object_shimshim_v01|g" "${DEVICE_BLOB_ROOT}"/vendor/bin/wcnss_service

"${MY_DIR}/setup-makefiles.sh"
