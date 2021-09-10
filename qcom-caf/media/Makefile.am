# Makefile.am - Automake script for mm-omxvideo
#
ACLOCAL_AMFLAGS = -I m4

BUILD_COMPONENTS := mm-core hypv-intercept

if BUILD_MM_VIDEO
BUILD_COMPONENTS += mm-video-v4l2
endif

SUBDIRS := $(BUILD_COMPONENTS)
