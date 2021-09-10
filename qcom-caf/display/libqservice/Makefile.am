h_sources = IQService.h \
            IQClient.h

cpp_sources = QService.cpp \
              IQService.cpp \
              IQClient.cpp \
              IQHDMIClient.cpp

library_includedir = $(includedir)
library_include_HEADERS = $(h_sources)

lib_LTLIBRARIES = libqservice.la
libqservice_la_CC = @CC@
libqservice_la_SOURCES = $(cpp_sources)
libqservice_la_CFLAGS = $(COMMON_CFLAGS) -DLOG_TAG=\"qdqservice\"
libqservice_la_CPPFLAGS = $(AM_CPPFLAGS)
libqservice_LDADD = -lhardware -lcutils -llog -lbinder
libqservice_la_LDFLAGS = -shared -avoid-version