CC=gcc
CXX=g++
OBJS=$(COBJS) $(CPPOBJS)
INCLUDES=-Ihidapi/hidapi -Iv7 -Ipopt
CFLAGS+=$(INCLUDES) -Wall -g -c 

LIBS=-framework IOKit -framework CoreFoundation -liconv
LIBPOPT=popt/.libs/libpopt.a

all: kpodd

kpodd: kpodd.o hidapi/mac/hid.o v7/v7.o $(LIBPOPT)
	$(CC) -Wall -g $^ -Lpopt $(LIBS) -o $@

hidapi/mac/hid.o: hidapi/mac/hid.c
	$(CC) $(CFLAGS) $< -o $@

v7/v7.o: v7/v7.c
	$(CC) -DV7_FORCE_STRICT_MODE -DV7_STACK_SIZE=32768 $(CFLAGS) $< -o $@

$(COBJS): %.o: %.c
	$(CC) $(CFLAGS) $< -o $@

popt-1.16.tar.gz:
	wget -o $@ http://rpm5.org/files/popt/popt-1.16.tar.gz

popt/STAMP: popt-1.16.tar.gz
	mkdir -p popt
	tar x --strip-components=1 -C popt -f $<
	touch $@
	
$(LIBPOPT): popt/STAMP	
	cd popt && ./configure --disable-shared --enable-static
	$(MAKE) -C popt clean all

dependencies: popt-1.16
	git submodule init
	git submodule update

clean:
	rm -f *.o hidtest $(CPPOBJS)

.PHONY: clean

