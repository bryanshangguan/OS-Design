# benchmarks/Makefile
CC      ?= gcc
CFLAGS  ?= -O2 -I.. -DUSE_WORKERS=1
LIBS    := -pthread
LDFLAGS := -L.. -lthread-worker

# Allow scheduler selection; default PSJF
SCHED ?= PSJF

# Default: build all benchmarks here
all: external_cal parallelCal vector_multiply

# Build the parent static lib first (and clean it) so it’s always fresh
../libthread-worker.a:
	$(MAKE) -C .. clean
	$(MAKE) -C .. SCHED=$(SCHED)

# NOTE: Output name matches README ("parallelCal")
parallelCal: parallel_cal.c ../libthread-worker.a
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@ $(LIBS)

external_cal: external_cal.c ../libthread-worker.a
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@ $(LIBS)

vector_multiply: vector_multiply.c ../libthread-worker.a
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@ $(LIBS)

# Optional test helper (if you still keep test.c here)
test: test.c ../libthread-worker.a
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@ $(LIBS)

clean:
	rm -f external_cal parallelCal vector_multiply test
	rm -rf record *.dSYM
