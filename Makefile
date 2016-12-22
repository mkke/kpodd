UNAME := $(shell uname -s)
HID_ARCH := unknown
ifeq ($(UNAME),Linux)
  HID_ARCH := linux
else ifneq ($(findstring MINGW32_NT, $(UNAME)),)
  HID_ARCH := windows
else ifeq ($(UNAME),Darwin)
  HID_ARCH := mac
endif

HID_INCLUDES_linux=$(shell pkg-config libusb-1.0 --cflags)
HID_INCLUDES=$(HID_INCLUDES_$(HID_ARCH)) -Ihidapi/hidapi
INCLUDES=$(HID_INCLUDES) -Iv7 -Ipopt

HID_LIBS_linux=$(shell pkg-config libudev --libs) -lrt
HID_LIBS_windows=
HID_LIBS_mac=-framework IOKit -framework CoreFoundation
HID_LIBS=$(HID_LIBS_$(HID_ARCH))
V7_LIBS_linux=-lm
V7_LIBS=$(V7_LIBS_$(HID_ARCH))
POPT_LIBS_mac=-liconv
POPT_LIBS=$(POPT_LIBS_$(HID_ARCH))
LIBS=$(HID_LIBS) $(V7_LIBS) $(POPT_LIBS)
LIBPOPT=popt/.libs/libpopt.a

CC=gcc
CXX=g++
OBJS=$(COBJS) $(CPPOBJS)
CFLAGS+=$(INCLUDES) -Wall -g -c --std=c99

all: kpodd

kpodd: kpodd.o hidapi/$(HID_ARCH)/hid.o v7/v7.o $(LIBPOPT)
	$(CC) -Wall -g $^ -Lpopt $(LIBS) -o $@

hidapi/$(HID_ARCH)/hid.o: hidapi/$(HID_ARCH)/hid.c
	$(CC) $(CFLAGS) $< -o $@

v7/v7.o: v7/v7.c
	$(CC) -DV7_FORCE_STRICT_MODE -DV7_STACK_SIZE=32768 $(CFLAGS) $< -o $@

$(COBJS): %.o: %.c
	$(CC) $(CFLAGS) $< -o $@

popt-1.16.tar.gz:
	wget -O $@ http://rpm5.org/files/popt/popt-1.16.tar.gz

popt/STAMP: popt-1.16.tar.gz
	mkdir -p popt
	tar x --strip-components=1 -C popt -f $<
	touch $@
	
$(LIBPOPT): popt/STAMP	
	cd popt && ./configure --disable-shared --enable-static
	$(MAKE) -C popt clean all

dependencies: popt-1.16.tar.gz
	git submodule init
	git submodule update

clean:
	rm -f *.o kpodd

.PHONY: clean

