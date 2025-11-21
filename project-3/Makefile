CC = gcc
CFLAGS = -g #-m32
AR = ar -rc
RANLIB = ranlib

# ----------------------------------------------------------------------
# Library
# ----------------------------------------------------------------------

all: libmy_vm.a

# Build static library from my_vm.o
libmy_vm.a: my_vm.o
	$(AR) $@ my_vm.o
	$(RANLIB) $@

# Compile my_vm.c into my_vm.o
my_vm.o: my_vm.c my_vm.h
	$(CC) $(CFLAGS) -c my_vm.c

# ----------------------------------------------------------------------
# Tests
# ----------------------------------------------------------------------

TEST_LIBS = my_vm.c -lpthread

test_alloc: tests/test_alloc.c my_vm.c my_vm.h
	$(CC) -g -o $@ tests/test_alloc.c $(TEST_LIBS)

test_putget: tests/test_putget.c my_vm.c my_vm.h
	$(CC) -g -o $@ tests/test_putget.c $(TEST_LIBS)

test_matmul: tests/test_matmul.c my_vm.c my_vm.h
	$(CC) -g -o $@ tests/test_matmul.c $(TEST_LIBS)

# Build all test binaries
tests: test_alloc test_putget test_matmul

# Build + RUN all tests
test: tests
	./test_alloc
	./test_putget
	./test_matmul

# ----------------------------------------------------------------------
# Cleanup
# ----------------------------------------------------------------------

clean:
	rm -rf *.o *.a test_alloc test_putget test_matmul
