# kext/Makefile - compile-check the Rage LT Pro kext against the 10.3
# Kernel framework.  Run on the Lombard (Panther/PPC, gcc 3.3):
#     make -C kext syntax     # validate against real IOKit headers
#     make -C kext objects    # produce .o (codegen); full link is later
#
# Full kext linking (-lkmod, kmod_info, bundle packaging) is a later step;
# this target just proves the sources compile against the IOKit C++ API.

CXX      ?= g++
KFRAME   := /System/Library/Frameworks/Kernel.framework
# Kernel headers searched first (win for IOKit/*); gcc's default /usr/include
# + builtin dirs supply arch base headers (ppc/ansi.h, stddef.h) the Kernel
# framework doesn't ship.  (Hence NOT -nostdinc.)
KINC     := -I$(KFRAME)/Headers -I$(KFRAME)/PrivateHeaders
KDEF     := -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT
KCXX     := -fno-rtti -fno-exceptions -fcheck-new -fno-builtin
IFLAGS   := -I. -I..
SRCS     := Mach64Accelerator.cpp Mach64GLUserClient.cpp

.PHONY: syntax objects clean

syntax:
	@for f in $(SRCS); do \
	  echo "  syntax: $$f"; \
	  $(CXX) -fsyntax-only $(KCXX) $(KDEF) $(KINC) $(IFLAGS) $$f || exit 1; \
	done
	@echo "kext sources: syntax OK"

objects:
	@for f in $(SRCS); do \
	  o=$${f%.cpp}.o; echo "  cc: $$f -> $$o"; \
	  $(CXX) -c $(KCXX) $(KDEF) $(KINC) $(IFLAGS) $$f -o $$o || exit 1; \
	done
	@echo "kext objects built"

clean:
	rm -f *.o
