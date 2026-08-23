# tools/Makefile - userspace helpers (build on the Lombard / any OS X PPC).
CC     ?= cc
CFLAGS ?= -O2 -Wall -std=gnu99

.PHONY: all clean
all: mach64_peek mach64_poke mach64_fill mach64_tri

mach64_tri: mach64_tri.c ../common/mach64_abi.h
	$(CC) $(CFLAGS) -o $@ mach64_tri.c -framework IOKit

mach64_peek: mach64_peek.c ../common/mach64_abi.h
	$(CC) $(CFLAGS) -o $@ mach64_peek.c -framework IOKit

mach64_poke: mach64_poke.c ../common/mach64_abi.h
	$(CC) $(CFLAGS) -o $@ mach64_poke.c -framework IOKit

mach64_fill: mach64_fill.c ../common/mach64_abi.h
	$(CC) $(CFLAGS) -o $@ mach64_fill.c -framework IOKit

clean:
	rm -f mach64_peek mach64_poke mach64_fill
