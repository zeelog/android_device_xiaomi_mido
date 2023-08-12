LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := android.hardware.gnss@2.1-impl-qti
# activate the following line for debug purposes only, comment out for production
#LOCAL_SANITIZE_DIAG += $(GNSS_SANITIZE_DIAG)
LOCAL_VENDOR_MODULE := true
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_SRC_FILES := \
    AGnss.cpp \
    Gnss.cpp \
    AGnssRil.cpp \
    GnssMeasurement.cpp \
    GnssConfiguration.cpp \
    GnssBatching.cpp \
    GnssGeofencing.cpp \
    GnssNi.cpp \
    GnssDebug.cpp \
    GnssAntennaInfo.cpp \
    MeasurementCorrections.cpp \
    GnssVisibilityControl.cpp

LOCAL_SRC_FILES += \
    location_api/GnssAPIClient.cpp \
    location_api/MeasurementAPIClient.cpp \
    location_api/GeofenceAPIClient.cpp \
    location_api/BatchingAPIClient.cpp \
    location_api/LocationUtil.cpp \

ifeq ($(GNSS_HIDL_LEGACY_MEASURMENTS),true)
LOCAL_CFLAGS += \
     -DGNSS_HIDL_LEGACY_MEASURMENTS
endif

LOCAL_C_INCLUDES:= \
    $(LOCAL_PATH)/location_api

LOCAL_HEADER_LIBRARIES := \
    libgps.utils_headers \
    libloc_core_headers \
    libloc_pla_headers \
    liblocation_api_headers \
    liblocbatterylistener_headers

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libhidlbase \
    libcutils \
    libutils \
    android.hardware.gnss@1.0 \
    android.hardware.gnss@1.1 \
    android.hardware.gnss@2.0 \
    android.hardware.gnss@2.1 \
    android.hardware.gnss.measurement_corrections@1.0 \
    android.hardware.gnss.measurement_corrections@1.1 \
    android.hardware.gnss.visibility_control@1.0 \
    android.hardware.health@1.0 \
    android.hardware.health@2.0 \
    android.hardware.health@2.1 \
    android.hardware.power@1.2 \
    libbase

LOCAL_SHARED_LIBRARIES += \
    libloc_core \
    libgps.utils \
    libdl \
    liblocation_api \

LOCAL_CFLAGS += $(GNSS_CFLAGS)
LOCAL_STATIC_LIBRARIES := liblocbatterylistener
LOCAL_STATIC_LIBRARIES += libhealthhalutils
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := android.hardware.gnss@2.1-service-qti

# activate the following line for debug purposes only, comment out for production
#LOCAL_SANITIZE_DIAG += $(GNSS_SANITIZE_DIAG)
LOCAL_VINTF_FRAGMENTS := android.hardware.gnss@2.1-service-qti.xml
LOCAL_VENDOR_MODULE := true
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_INIT_RC := android.hardware.gnss@2.1-service-qti.rc
LOCAL_SRC_FILES := \
    service.cpp \

LOCAL_HEADER_LIBRARIES := \
    libgps.utils_headers \
    libloc_core_headers \
    libloc_pla_headers \
    liblocation_api_headers


LOCAL_SHARED_LIBRARIES := \
    liblog \
    libcutils \
    libdl \
    libbase \
    libutils \
    libgps.utils \
    libqti_vndfwk_detect \

LOCAL_SHARED_LIBRARIES += \
    libhidlbase \
    android.hardware.gnss@1.0 \
    android.hardware.gnss@1.1 \
    android.hardware.gnss@2.0 \
    android.hardware.gnss@2.1 \

LOCAL_CFLAGS += $(GNSS_CFLAGS)

ifneq ($(LOC_HIDL_VERSION),)
LOCAL_CFLAGS += -DLOC_HIDL_VERSION='"$(LOC_HIDL_VERSION)"'
endif

include $(BUILD_EXECUTABLE)
