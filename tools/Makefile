# tools/Makefile - userspace helpers (build on the Lombard / any OS X PPC).
CC     ?= cc
CFLAGS ?= -O2 -Wall -std=gnu99

.PHONY: all clean
all: mach64_peek mach64_poke mach64_fill mach64_tri mach64_gouraud mach64_dmatest mach64_ringtest mach64_gldtest mach64_primtest

# links the REAL gld translation sources to prove them on hardware
GLD_SRC  := ../gldriver/gld_render.c ../gldriver/gld_state.c ../common/mach64_emit.c
GLD_HDRS := ../gldriver/gld.h ../common/mach64_abi.h ../common/mach64_regs.h
# render-slot bodies + dispatch install
PRIM_SRC := ../gldriver/gld_prims.c ../gldriver/gld_dispatch.c

mach64_gldtest: mach64_gldtest.c $(GLD_SRC) $(GLD_HDRS)
	$(CC) $(CFLAGS) -I.. -o $@ mach64_gldtest.c $(GLD_SRC) -framework IOKit

mach64_primtest: mach64_primtest.c $(GLD_SRC) $(PRIM_SRC) $(GLD_HDRS)
	$(CC) $(CFLAGS) -I.. -o $@ mach64_primtest.c $(GLD_SRC) $(PRIM_SRC) -framework IOKit

mach64_ringtest: mach64_ringtest.c ../common/mach64_abi.h ../common/mach64_regs.h
	$(CC) $(CFLAGS) -o $@ mach64_ringtest.c -framework IOKit

mach64_tri: mach64_tri.c ../common/mach64_abi.h
	$(CC) $(CFLAGS) -o $@ mach64_tri.c -framework IOKit

mach64_gouraud: mach64_gouraud.c ../common/mach64_abi.h
	$(CC) $(CFLAGS) -o $@ mach64_gouraud.c -framework IOKit

mach64_dmatest: mach64_dmatest.c ../common/mach64_abi.h
	$(CC) $(CFLAGS) -o $@ mach64_dmatest.c -framework IOKit

mach64_peek: mach64_peek.c ../common/mach64_abi.h
	$(CC) $(CFLAGS) -o $@ mach64_peek.c -framework IOKit

mach64_poke: mach64_poke.c ../common/mach64_abi.h
	$(CC) $(CFLAGS) -o $@ mach64_poke.c -framework IOKit

mach64_fill: mach64_fill.c ../common/mach64_abi.h
	$(CC) $(CFLAGS) -o $@ mach64_fill.c -framework IOKit

clean:
	rm -f mach64_peek mach64_poke mach64_fill
