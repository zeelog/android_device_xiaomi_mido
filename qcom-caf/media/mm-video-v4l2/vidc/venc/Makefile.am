#AM_CFLAGS = -Wall
#AM_CFLAGS = -Wundef
#AM_CFLAGS += -Wstrict-prototypes
#AM_CFLAGS += -Wno-trigraphs
#AM_CFLAGS += -Wno-multichar

AM_CPPFLAGS = -D__alignx\(x\)=__attribute__\(\(__aligned__\(x\)\)\)
AM_CPPFLAGS += -D__align=__alignx
AM_CPPFLAGS += -Dinline=__inline
AM_CPPFLAGS += -DIMAGE_APPS_PROC
AM_CPPFLAGS += -DCDECL
AM_CPPFLAGS += -DT_ARM
AM_CPPFLAGS += -DNO_ARM_CLZ
AM_CPPFLAGS += -D_ANDROID_
AM_CPPFLAGS += -UENABLE_DEBUG_LOW
AM_CPPFLAGS += -DENABLE_DEBUG_HIGH
AM_CPPFLAGS += -DENABLE_DEBUG_ERROR
AM_CPPFLAGS += -UINPUT_BUFFER_LOG
AM_CPPFLAGS += -UOUTPUT_BUFFER_LOG
AM_CPPFLAGS += -Werror
AM_CPPFLAGS += -D_ANDROID_ICS_
AM_CPPFLAGS += -D_MSM8974_

#Hypervisor
if TARGET_HYPERVISOR
AM_CPPFLAGS += -D_HYPERVISOR_
endif

AM_CPPFLAGS += -DUSE_ION
#AM_CPPFLAGS += "-include stdint.h"
AM_CPPFLAGS += "-Dstrlcpy=g_strlcpy"
AM_CPPFLAGS += "-Dstrlcat=g_strlcat"
AM_CPPFLAGS += "-std=c++11"
AM_CPPFLAGS += -Wno-undef
AM_CPPFLAGS += -Wno-multichar
AM_CPPFLAGS += -g -O3
AM_CPPFLAGS += "-DHAVE_ANDROID_OS"
AM_CPPFLAGS += "-DUSE_CAMERA_METABUFFER_UTILS"

if USE_GLIB
AM_CPPFLAGS += -D_USE_GLIB_
endif

if TARGET_MSM8610
AM_CPPFLAGS += -DMAX_RES_720P
AM_CPPFLAGS += -D_MSM8610_
else
if TARGET_MSM8226
AM_CPPFLAGS += -DMAX_RES_1080P
else
AM_CPPFLAGS += -DMAX_RES_1080P
AM_CPPFLAGS += -DMAX_RES_1080P_EBI
endif
endif

if TARGETS_THAT_SUPPORT_PQ
AM_CPPFLAGS += -D_PQ_
endif

if TARGETS_THAT_SUPPORT_UBWC
AM_CPPFLAGS += -D_UBWC_
endif

if TARGETS_THAT_SUPPORT_VQZIP
AM_CPPFLAGS += -D_VQZIP_
endif

if TARGETS_THAT_USE_FLAG_MSM8226
AM_CPPFLAGS += -D_MSM8226_
endif

if TARGET_USES_MEDIA_EXTENSIONS
AM_CPPFLAGS += -DUSE_NATIVE_HANDLE_SOURCE
AM_CPPFLAGS += -DSUPPORT_CONFIG_INTRA_REFRESH
endif

if MASTER_SIDE_CP_TARGET_LIST
AM_CPPFLAGS += -DMASTER_SIDE_CP
endif

AM_CPPFLAGS += -I$(top_srcdir)/mm-video-v4l2/vidc/common/inc/
AM_CPPFLAGS += -I$(top_srcdir)/mm-video-v4l2/vidc/venc/inc/
AM_CPPFLAGS += -I$(top_srcdir)/libc2dcolorconvert/
AM_CPPFLAGS += -I$(top_srcdir)/mm-core/inc/
AM_CPPFLAGS += -I$(top_srcdir)/mm-core/src/common/
AM_CPPFLAGS += -I$(top_srcdir)/libstagefrighthw/
AM_CPPFLAGS += -I$(top_srcdir)/hypv-intercept

sources = src/omx_video_base.cpp
sources += src/omx_video_encoder.cpp
sources += src/video_encoder_device_v4l2.cpp
sources += $(top_srcdir)/mm-video-v4l2/vidc/common/src/extra_data_handler.cpp
sources += $(top_srcdir)/mm-video-v4l2/vidc/common/src/vidc_color_converter.cpp

lib_LTLIBRARIES = libOmxVenc.la
libOmxVenc_la_SOURCES = $(sources)
libOmxVenc_la_CFLAGS = $(AM_CFLAGS) $(AM_CPPFLAGS) -fPIC
#libOmxVenc_la_LDLIBS = ../libc2d2colorconvert/libc2dcolorconvert.la
#libOmxVenc_la_LIBADD  = ../../../mm-core/libOmxCore.la
libOmxVenc_la_LDFLAGS = -lstdc++ -lpthread -llog -lutils -lbinder -lcutils -lglib-2.0 -lbase -ldl -lpthread -shared -lqdMetaData
#  -lc2d2 -lgui -lOmxCore -lgpustats -ldl -lpthread
libOmxVenc_la_LDFLAGS += -version-info 0
libOmxVenc_la_LDFLAGS += -L$(top_builddir)/hypv-intercept
libOmxVenc_la_LDFLAGS += -lhypv_intercept
