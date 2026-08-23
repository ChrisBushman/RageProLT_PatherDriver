# RageProLT Panther Driver - build
#
# hosttest : builds/runs on THIS machine (x86_64/arm64) to verify the
#            portable emission core.  This is what `make` builds by default.
#
# kext / gldriver : the real driver.  These CANNOT be built on a modern
#            machine - they need a PowerPC + Mac OS X 10.3 toolchain and the
#            10.3 IOKit / OpenGL private headers.  Targets are provided for
#            documentation; see README.md "Building the real driver".

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c99
COMMON   = common

.PHONY: all hosttest clean kext gldriver

all: hosttest

# ---- host-verifiable portable core ----------------------------------
hosttest: build/test_triangle
	./build/test_triangle

build/test_triangle: hosttest/test_triangle.c $(COMMON)/mach64_emit.c \
                     $(COMMON)/mach64_emit.h $(COMMON)/mach64_regs.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I$(COMMON) -o $@ hosttest/test_triangle.c $(COMMON)/mach64_emit.c

clean:
	rm -rf build

# ---- real driver (requires PPC / 10.3 SDK; see README) --------------
PPC_SDK   ?= /Developer/SDKs/MacOSX10.3.9.sdk
PPC_CC    ?= powerpc-apple-darwin7-gcc      # cross toolchain, not present here

kext:
	@echo "kext build requires the 10.3 PPC toolchain + IOKit headers."
	@echo "On a Panther build host:  cd kext && xcodebuild (or gcc -arch ppc"
	@echo "  -isysroot $(PPC_SDK) -I.../IOKit -bundle ... Mach64GLUserClient.cpp)"
	@false

gldriver:
	@echo "gldriver bundle requires the 10.3 PPC toolchain + OpenGL private hdrs."
	@echo "Build as a CFBundle exporting the gld* symbols (see gldriver/gld.h)."
	@false
