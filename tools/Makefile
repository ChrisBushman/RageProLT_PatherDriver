# tools/Makefile - userspace helpers (build on the Lombard / any OS X PPC).
CC     ?= cc
CFLAGS ?= -O2 -Wall -std=gnu99

.PHONY: all clean
all: mach64_peek

mach64_peek: mach64_peek.c ../common/mach64_abi.h
	$(CC) $(CFLAGS) -o $@ mach64_peek.c -framework IOKit

clean:
	rm -f mach64_peek
