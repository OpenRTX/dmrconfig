CC		= gcc

VERSION         = 0.1
GITCOUNT        = $(shell git rev-list HEAD --count)
CFLAGS		= -g -O -Wall -Werror -DVERSION='"$(VERSION).$(GITCOUNT)"'
LDFLAGS		= -g

OBJS		= main.o util.o radio.o dfu.o uv380.o md380.o
LIBS            = -lusb-1.0

# Mac OS X
#CFLAGS          += -I/usr/local/opt/gettext/include
#LIBS            += -L/usr/local/opt/gettext/lib -lintl

all:		dmrconfig

dmrconfig:	$(OBJS)
		$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

clean:
		rm -f *~ *.o core dmrconfig

install:	dmrconfig
		install -c -s dmrconfig /usr/local/bin/dmrconfig

dmrconfig.linux: dmrconfig
		cp -p $< $@
		strip $@

###
dfu.o: dfu.c util.h
main.o: main.c radio.h util.h
md380.o: md380.c radio.h util.h
radio.o: radio.c radio.h util.h
util.o: util.c util.h
uv380.o: uv380.c radio.h util.h
