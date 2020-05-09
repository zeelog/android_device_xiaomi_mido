#!/bin/bash
#
# Copyright (C) 2016 The CyanogenMod Project
# Copyright (C) 2017, 2019 The LineageOS Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# If we're being sourced by the common script that we called,
# stop right here. No need to go down the rabbit hole.
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

LINEAGE_ROOT="${MY_DIR}/../../.."

HELPER="$LINEAGE_ROOT/vendor/lineage/build/tools/extract_utils.sh"
if [ ! -f "$HELPER" ]; then
    echo "Unable to find helper script at $HELPER"
    exit 1
fi
source "${HELPER}"

# Default to sanitizing the vendor folder before extraction
CLEAN_VENDOR=true

SECTION=
KANG=

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
setup_vendor "${DEVICE}" "${VENDOR}" "${LINEAGE_ROOT}" false "${CLEAN_VENDOR}"

extract "${MY_DIR}"/proprietary-files.txt "${SRC}" \
        "${KANG}" --section "${SECTION}"

DEVICE_BLOB_ROOT="${LINEAGE_ROOT}"/vendor/"${VENDOR}"/"${DEVICE}"/proprietary

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

# Wcnss_service - libqmiservices_shim
patchelf --add-needed "libqmiservices_shim.so" "${DEVICE_BLOB_ROOT}"/vendor/bin/wcnss_service
sed -i "s|dms_get_service_object_internal_v01|dms_get_service_object_shimshim_v01|g" "${DEVICE_BLOB_ROOT}"/vendor/bin/wcnss_service

"${MY_DIR}/setup-makefiles.sh"
