# sources and intermediate files are separated

#AM_CFLAGS = -Wall
#AM_CFLAGS += -Wundef
#AM_CFLAGS += -Wstrict-prototypes
#AM_CFLAGS += -Wno-trigraphs
#AM_CFLAGS += -g -O3
#AM_CFLAGS += -O0 -fno-inline -fno-short-enums
#AM_CFLAGS += -fPIC

AM_CPPFLAGS = -D__packed__=
AM_CPPFLAGS += -D_ANDROID_
AM_CPPFLAGS += -U_ENABLE_QC_MSG_LOG_
#AM_CPPFLAGS += -g -O3
#AM_CPPFLAGS += -O0 -fno-inline -fno-short-enums
AM_CPPFLAGS += -I$(top_srcdir)/mm-core/inc/
AM_CPPFLAGS += -I$(top_srcdir)/mm-core/src/common/
AM_CPPFLAGS += "-Dstrlcpy=g_strlcpy"
AM_CPPFLAGS += "-Dstrlcat=g_strlcat"

h_sources  =inc/OMX_Audio.h
h_sources +=inc/OMX_Component.h
h_sources +=inc/OMX_ContentPipe.h
h_sources +=inc/OMX_Core.h
h_sources +=inc/OMX_Image.h
h_sources +=inc/OMX_Index.h
h_sources +=inc/OMX_IVCommon.h
h_sources +=inc/OMX_Other.h
h_sources +=inc/OMX_QCOMExtns.h
h_sources +=inc/OMX_Types.h
h_sources +=inc/OMX_Video.h
h_sources +=inc/qc_omx_common.h
h_sources +=inc/qc_omx_component.h
h_sources +=inc/qc_omx_msg.h
h_sources +=inc/QOMX_AudioExtensions.h
h_sources +=inc/QOMX_AudioIndexExtensions.h
h_sources +=inc/OMX_CoreExt.h
h_sources +=inc/QOMX_CoreExtensions.h
h_sources +=inc/QOMX_FileFormatExtensions.h
h_sources +=inc/QOMX_IVCommonExtensions.h
h_sources +=inc/QOMX_SourceExtensions.h
h_sources +=inc/QOMX_VideoExtensions.h
h_sources +=inc/OMX_IndexExt.h
h_sources +=inc/QOMX_StreamingExtensions.h

c_sources  =src/common/omx_core_cmp.cpp
c_sources +=src/common/qc_omx_core.c

if TARGET_MSM8953
TARGET_REGISTRY = msm8953
endif

if TARGET_MSM8996
TARGET_REGISTRY = msm8996
endif

if TARGET_MSM8909
TARGET_REGISTRY = 8909
endif

if TARGET_MSM8937
TARGET_REGISTRY = 8937
endif

c_sources +=src/${TARGET_REGISTRY}/registry_table_android.c

lib_LTLIBRARIES = libOmxCore.la
include_HEADERS = $(h_sources)
libOmxCore_la_SOURCES = $(c_sources)
libOmxCore_la_CFLAGS = $(AM_CFLAGS) $(AM_CPPFLAGS) -fPIC
libOmxCore_la_CPPFLAGS = $(AM_CFLAGS) $(AM_CPPFLAGS) -fPIC
libOmxCore_la_LDFLAGS = -ldl -lrt -lpthread -lglib-2.0 -lcutils
libOmxCore_la_LDFLAGS += -shared -avoid-version

lib_LTLIBRARIES += libmm-omxcore.la

c_sources  =src/common/omx_core_cmp.cpp
c_sources +=src/common/qc_omx_core.c
c_sources +=src/${TARGET_REGISTRY}/registry_table.c

include_HEADERS = $(h_sources)
libmm_omxcore_la_SOURCES = $(c_sources)
libmm_omxcore_la_CFLAGS = $(AM_CFLAGS) $(AM_CPPFLAGS) -fPIC
libmm_omxcore_la_CPPFLAGS = $(AM_CFLAGS) $(AM_CPPFLAGS) -fPIC
libmm_omxcore_la_LDFLAGS = -ldl -lrt -lpthread -lglib-2.0 -lcutils
libmm_omxcore_la_LDFLAGS += -shared -avoid-version

