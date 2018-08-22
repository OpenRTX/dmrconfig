CC		= gcc -m32

VERSION         = 1.0
CFLAGS		= -g -O0 -Wall -Werror -DVERSION='"$(VERSION)"'
LDFLAGS		= -g

OBJS		= main.o util.o radio.o uv380.o
SRCS		= main.c util.c radio.c uv380.c
LIBS            =

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
main.o: main.c radio.h util.h
radio.o: radio.c radio.h util.h
util.o: util.c util.h
uv380.o: uv380.c radio.h util.h
