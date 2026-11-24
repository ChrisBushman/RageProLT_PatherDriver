# RageProLT Panther driver - top-level build
#
#   make            host tests (run on any machine: Linux, macOS, the G4)
#   make hosttest   same
#   make kext       build ATIRageProLT.kext - run on a Panther PPC host (the G4 rig); never loads anything
#   make checkfp    kext + fail on any floating-point instruction (run on the Panther host)
#
# The kext needs the 10.3 Kernel framework and gcc 3.3, so it cannot be built on a modern machine; kext/Makefile documents the flags.

CC     ?= cc
CFLAGS ?= -O2 -Wall -W -std=gnu99
COMMON  = common
B       = build

.PHONY: all hosttest clean kext checkfp features-check

all: hosttest

hosttest: $(B)/test_triangle $(B)/guard_test $(B)/vram_span_test $(B)/cgl_attribs_test
	./$(B)/test_triangle
	./$(B)/guard_test
	./$(B)/vram_span_test
	./$(B)/cgl_attribs_test

$(B)/test_triangle: hosttest/test_triangle.c $(COMMON)/mach64_emit.c $(COMMON)/mach64_emit.h $(COMMON)/mach64_regs.h
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -I$(COMMON) -o $@ hosttest/test_triangle.c $(COMMON)/mach64_emit.c

$(B)/guard_test: tests/guard_test.c $(COMMON)/rage_guard.h $(COMMON)/rage_vram.h $(COMMON)/rage_types.h $(COMMON)/mach64_regs.h
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -I$(COMMON) -o $@ tests/guard_test.c

$(B)/vram_span_test: tests/vram_span_test.c $(COMMON)/mach64_regs.h
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -I. -I$(COMMON) -o $@ tests/vram_span_test.c

$(B)/cgl_attribs_test: tests/cgl_attribs_test.c $(COMMON)/cgl_attribs.h
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -I. -I$(COMMON) -o $@ tests/cgl_attribs_test.c

# Release builds must keep 2D acceleration on (software 2D is not acceptable).
features-check:
	@grep -q 'define RAGE_FEAT_2D_GA       1' config/features.h && echo "features-check: RAGE_FEAT_2D_GA default is on"

kext:
	$(MAKE) -C kext kext
checkfp:
	$(MAKE) -C kext checkfp

clean:
	rm -rf $(B)
	-$(MAKE) -C kext clean
