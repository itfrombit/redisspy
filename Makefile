# redisspy Makefile
#
# Copyright (C) 2010 Jeff Buck 
# http://github.com/itfrombit/redisspy
#
# This file is released under the BSD license, see the COPYING file

HIREDIS_ROOT=../3rd/hiredis

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
OPTIMIZATION?=-O2
ifeq ($(uname_S),SunOS)
  CFLAGS?= -std=c99 -pedantic $(OPTIMIZATION) -Wall -W -D__EXTENSIONS__ -D_XPG6
  CCLINK?= -ldl -lnsl -lsocket -lm -lpthread
else
  CFLAGS?= -std=c99 -pedantic $(OPTIMIZATION) -Wall -W -Wstrict-prototypes -Wwrite-strings $(ARCH) $(PROF) -I$(HIREDIS_ROOT)
  CCLINK?= -lm -lpthread -lncurses
endif
CCOPT= $(CFLAGS) $(CCLINK) $(ARCH) $(PROF)

#DEBUG?= -g -rdynamic -ggdb 
DEBUG?= -g -ggdb 

HIREDIS_OBJ = $(HIREDIS_ROOT)/net.o $(HIREDIS_ROOT)/hiredis.o $(HIREDIS_ROOT)/sds.o
SPY_OBJ = spymodel.o spywindow.o spycontroller.o main.o spydetailcontroller.o spyhelpcontroller.o

SPYNAME = redisspy

all: redisspy

#redisspy: $(SPY_OBJ)
redisspy: $(HIREDIS_OBJ) $(SPY_OBJ)
	$(CC) -g -o $(SPYNAME) $(CCOPT) $(DEBUG) $(HIREDIS_OBJ) $(SPY_OBJ)

.c.o:
	$(CC) -g -c $(CFLAGS) $(DEBUG) $(COMPILE_TIME) $<

install:
	cp $(SPYNAME) /usr/local/bin

clean:
	rm -rf $(SPYNAME) *.o *.gcda *.gcno *.gcov

dep:
	$(CC) -MM *.c

32bit:
	@echo ""
	@echo "WARNING: if it fails under Linux you probably need to install libc6-dev-i386"
	@echo ""
	make ARCH="-m32"

gprof:
	make PROF="-pg"

gcov:
	make PROF="-fprofile-arcs -ftest-coverage"

noopt:
	make OPTIMIZATION=""

