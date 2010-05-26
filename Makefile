# redisspy Makefile
#
# Copyright (C) 2010 Jeff Buck 
# http://github.com/itfrombit/redisspy
#
# This file is released under the BSD license, see the COPYING file

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
OPTIMIZATION?=-O2
ifeq ($(uname_S),SunOS)
  CFLAGS?= -std=c99 -pedantic $(OPTIMIZATION) -Wall -W -D__EXTENSIONS__ -D_XPG6
  CCLINK?= -ldl -lnsl -lsocket -lm -lpthread
else
  CFLAGS?= -std=c99 -pedantic $(OPTIMIZATION) -Wall -W $(ARCH) $(PROF) -I../hiredis
  CCLINK?= -lm -pthread -lncurses
endif
CCOPT= $(CFLAGS) $(CCLINK) $(ARCH) $(PROF)
DEBUG?= -g -rdynamic -ggdb 

DUMPOBJ = ../hiredis/anet.o ../hiredis/hiredis.o ../hiredis/sds.o redisdump.o
SPYOBJ = ../hiredis/anet.o ../hiredis/hiredis.o ../hiredis/sds.o redisspy.o

DUMPNAME = redisdump
SPYNAME = redisspy

all: redisspy redisdump

# Deps (use make dep to generate this)
redisspy.o: redisspy.c

redisspy: $(SPYOBJ)
	$(CC) -o $(SPYNAME) $(CCOPT) $(DEBUG) $(SPYOBJ)

redisdump.o: redisdump.c

redisdump: $(DUMPOBJ)
	$(CC) -o $(DUMPNAME) $(CCOPT) $(DEBUG) $(DUMPOBJ)

.c.o:
	$(CC) -c $(CFLAGS) $(DEBUG) $(COMPILE_TIME) $<

clean:
	rm -rf $(DUMPNAME) $(SPYNAME) *.o *.gcda *.gcno *.gcov

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
