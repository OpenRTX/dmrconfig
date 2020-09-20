CC             ?= gcc
PKG_CONFIG     ?= pkg-config

VERSION         = $(shell git describe --tags --abbrev=0)
GITCOUNT        = $(shell git rev-list HEAD --count)
UNAME           = $(shell uname)

OBJS            = main.o util.o radio.o dfu-libusb.o uv380.o md380.o rd5r.o \
                  gd77.o hid.o serial.o d868uv.o dm1801.o
CFLAGS         ?= -g -O -Wall -Werror 
CFLAGS         += -DVERSION='"$(VERSION).$(GITCOUNT)"' \
                  $(shell $(PKG_CONFIG) --cflags libusb-1.0)
LDFLAGS        ?= -g
LIBS            = $(shell $(PKG_CONFIG) --libs --static libusb-1.0)

#
# Make sure pkg-config is installed.
#
ifeq ($(shell $(PKG_CONFIG) --version),)
    $(error Fatal error: pkg-config is not installed)
endif

#
# Linux
#
# To install required libraries, use:
#   sudo apt-get install pkg-config libusb-1.0-0-dev libudev-dev
#
ifeq ($(UNAME),Linux)
    OBJS        += hid-libusb.o

    # Link libusb statically, when possible
    LIBUSB      = /usr/lib/x86_64-linux-gnu/libusb-1.0.a
    ifeq ($(wildcard $(LIBUSB)),$(LIBUSB))
        LIBS    = $(LIBUSB) -lpthread -ludev
    endif
endif

#
# Mac OS X
#
# To install required libraries, use:
#   brew install pkg-config libusb
#
ifeq ($(UNAME),Darwin)
    OBJS        += hid-macos.o
    LIBS        += -framework IOKit -framework CoreFoundation
endif

all:		dmrconfig

dmrconfig:	$(OBJS)
		$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

clean:
		rm -f *~ *.o core dmrconfig dmrconfig.exe

install:	dmrconfig
		install -c -s dmrconfig /usr/local/bin/dmrconfig

###
d868uv.o: d868uv.c radio.h util.h d868uv-map.h
dfu-libusb.o: dfu-libusb.c util.h
dfu-windows.o: dfu-windows.c util.h
gd77.o: gd77.c radio.h util.h
hid.o: hid.c util.h
hid-libusb.o: hid-libusb.c util.h
hid-macos.o: hid-macos.c util.h
hid-windows.o: hid-windows.c util.h
main.o: main.c radio.h util.h
md380.o: md380.c radio.h util.h
radio.o: radio.c radio.h util.h
rd5r.o: rd5r.c radio.h util.h
serial.o: serial.c util.h
util.o: util.c util.h
uv380.o: uv380.c radio.h util.h
