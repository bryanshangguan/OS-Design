CC      = gcc
# Supressed the deprecated warnings 
CFLAGS  = -g -c -Wall -Wextra -O2 -I. -DUSE_WORKERS=1 -Wno-deprecated-declarations 
AR      = ar -rc
RANLIB  = ranlib

all: clean thread-worker.a

thread-worker.a: thread-worker.o
	$(AR) libthread-worker.a thread-worker.o
	$(RANLIB) libthread-worker.a

thread-worker.o: thread-worker.h
ifeq ($(SCHED), PSJF)
	$(CC) -pthread $(CFLAGS) -DPSJF thread-worker.c -o thread-worker.o
else ifeq ($(SCHED), MLFQ)
	$(CC) -pthread $(CFLAGS) -DMLFQ thread-worker.c -o thread-worker.o
else ifeq ($(SCHED), CFS)
	$(CC) -pthread $(CFLAGS) -DCFS thread-worker.c -o thread-worker.o
else
	@echo "no such scheduling algorithm."
	@false
endif

clean:
	rm -rf testfile *.o *.a
