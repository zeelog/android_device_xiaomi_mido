
ACLOCAL_AMFLAGS = -I m4
bin_PROGRAMS = hdmi_in_test
bin_PROGRAMS += fmt_change_test
pkgconfigdir = $(libdir)/pkgconfig

REC_INCLUDES = -I $(top_srcdir)/qahw_api/inc
REC_INCLUDES += -I $(top_srcdir)/qahw/inc

hdmi_in_test_SOURCES = src/hdmi_in_event_test.c
hdmi_in_test_CFLAGS  = $(CFLAGS) -Wno-sign-compare -Werror
hdmi_in_test_LDADD = -llog -lpthread

fmt_change_test_SOURCES = src/fmt_change_test.c
fmt_change_test_CFLAGS  = $(CFLAGS) -Wno-sign-compare -Werror $(REC_INCLUDES)
fmt_change_test_LDADD = -llog -lpthread ../qahw_api/libqahw.la

