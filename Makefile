ARCH := unknown
KPODD_EXE := kpodd
UNAME := $(shell uname -s)
ifeq ($(UNAME),Linux)
  ARCH := linux
else ifneq ($(findstring MINGW32_NT, $(UNAME)),)
  ARCH := windows
  KPODD_EXE := kpodd.exe
else ifeq ($(UNAME),Darwin)
  ARCH := mac
endif
ifeq ($(findstring mingw32,$(CC)),mingw32)
  ARCH := windows
  KPODD_EXE := kpodd.exe
endif

HID_INCLUDES_linux=$(shell pkg-config libusb-1.0 --cflags)
HID_INCLUDES=$(HID_INCLUDES_$(ARCH)) -Ihidapi/hidapi
INCLUDES=$(HID_INCLUDES) -Iv7 -Ipopt

HID_LIBS_linux=$(shell pkg-config libudev --libs) -lrt
HID_LIBS_windows=-lsetupapi
HID_LIBS_mac=-framework IOKit -framework CoreFoundation
HID_LIBS=$(HID_LIBS_$(ARCH))
V7_LIBS_linux=-lm
V7_LIBS=$(V7_LIBS_$(ARCH))
POPT_LIBS_mac=-liconv
POPT_LIBS=$(POPT_LIBS_$(ARCH))
LIBS=$(HID_LIBS) $(V7_LIBS) $(POPT_LIBS)
LIBPOPT=popt/.libs/libpopt.a

OBJS=$(COBJS) $(CPPOBJS)
CFLAGS+=$(INCLUDES) -Wall -g -c --std=c99

all: $(KPODD_EXE)

kpodd-mingw32:
	CC=i686-w64-mingw32-gcc POPT_CONF=--host=i686-w64-mingw32 $(MAKE) kpodd.exe

$(KPODD_EXE): kpodd.o hidapi/$(ARCH)/hid.o v7/v7-$(ARCH).o $(LIBPOPT)
	$(CC) -Wall -g $^ -Lpopt $(LIBS) -o $@

hidapi/$(ARCH)/hid.o: hidapi/$(ARCH)/hid.c
	$(CC) $(CFLAGS) $< -o $@

v7/v7-$(ARCH).o: v7/v7.c
	$(CC) -DV7_FORCE_STRICT_MODE -DV7_STACK_SIZE=32768 $(CFLAGS) $< -o $@

$(COBJS): %.o: %.c
	$(CC) $(CFLAGS) $< -o $@

popt-1.16.tar.gz:
	wget -O $@ http://rpm5.org/files/popt/popt-1.16.tar.gz

popt/STAMP: popt-1.16.tar.gz
	mkdir -p popt
	tar x --strip-components=1 -C popt -f $<
	cd popt && patch < ../poptconfig_win32.diff
	cd popt && patch < ../popthelp_win32.diff
	touch $@
	
$(LIBPOPT): popt/STAMP	
	cd popt && ./configure --disable-shared --enable-static $(POPT_CONF)
	$(MAKE) -C popt clean all

dependencies: popt-1.16.tar.gz
	git submodule init
	git submodule update

clean:
	rm -f *.o v7/v7-$(ARCH).o hidapi/$(ARCH)/hid.o kpodd
	cd popt && make distclean

.PHONY: clean

