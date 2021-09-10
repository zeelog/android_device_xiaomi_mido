# ---------------------------------------------------------------------------------
#				MM-AUDIO-OSS-8K-AENC-G7111
# ---------------------------------------------------------------------------------

# cross-compiler flags
CFLAGS += -Wall
CFLAGS += -Wundef
CFLAGS += -Wstrict-prototypes
CFLAGS += -Wno-trigraphs

# cross-compile flags specific to shared objects
CFLAGS_SO += -fpic

# required pre-processor flags
CPPFLAGS := -D__packed__=
CPPFLAGS += -DIMAGE_APPS_PROC
CPPFLAGS += -DFEATURE_Q_SINGLE_LINK
CPPFLAGS += -DFEATURE_Q_NO_SELF_QPTR
CPPFLAGS += -DFEATURE_LINUX
CPPFLAGS += -DFEATURE_NATIVELINUX
CPPFLAGS += -DFEATURE_DSM_DUP_ITEMS

CPPFLAGS += -g
CPPFALGS += -D_DEBUG
CPPFLAGS += -Iinc

# linker flags
LDFLAGS += -L$(SYSROOT)/usr/lib

# linker flags for shared objects
LDFLAGS_SO := -shared

# defintions
LIBMAJOR := $(basename $(basename $(LIBVER)))
LIBINSTALLDIR := $(DESTDIR)usr/lib
INCINSTALLDIR := $(DESTDIR)usr/include
BININSTALLDIR := $(DESTDIR)usr/bin

# ---------------------------------------------------------------------------------
#					BUILD
# ---------------------------------------------------------------------------------
all: libOmxG711Enc.so.$(LIBVER) mm-aenc-omxg711-test

install:
	echo "intalling aenc-g711 in $(DESTDIR)"
	if [ ! -d $(LIBINSTALLDIR) ]; then mkdir -p $(LIBINSTALLDIR); fi
	if [ ! -d $(INCINSTALLDIR) ]; then mkdir -p $(INCINSTALLDIR); fi
	if [ ! -d $(BININSTALLDIR) ]; then mkdir -p $(BININSTALLDIR); fi
	install -m 555 libOmxG711Enc.so.$(LIBVER) $(LIBINSTALLDIR)
	cd $(LIBINSTALLDIR) && ln -s libOmxG711Enc.so.$(LIBVER) libOmxG711Enc.so.$(LIBMAJOR)
	cd $(LIBINSTALLDIR) && ln -s libOmxG711Enc.so.$(LIBMAJOR) libOmxG711Enc.so
	install -m 555 mm-aenc-omxg711-test $(BININSTALLDIR)

# ---------------------------------------------------------------------------------
#				COMPILE LIBRARY
# ---------------------------------------------------------------------------------
LDLIBS := -lpthread
LDLIBS += -lstdc++
LDLIBS += -lOmxCore

SRCS := src/omx_g711_aenc.cpp
SRCS += src/aenc_svr.c

libOmxG711Enc.so.$(LIBVER): $(SRCS)
	$(CC) $(CPPFLAGS) $(CFLAGS_SO) $(LDFLAGS_SO) -Wl,-soname,libOmxG711Enc.so.$(LIBMAJOR) -o $@ $^ $(LDFLAGS) $(LDLIBS)

# ---------------------------------------------------------------------------------
#				COMPILE TEST APP
# ---------------------------------------------------------------------------------
TEST_LDLIBS := -lpthread
TEST_LDLIBS += -ldl
TEST_LDLIBS += -lOmxCore

#TEST_SRCS := test/omx_g711_enc_test.c

mm-aenc-omxg711-test: libOmxG711Enc.so.$(LIBVER) $(TEST_SRCS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^ $(TEST_LDLIBS)

# ---------------------------------------------------------------------------------
#					END
# ---------------------------------------------------------------------------------
