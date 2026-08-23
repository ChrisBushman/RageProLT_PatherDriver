# kext/Makefile - build the Rage LT Pro kext on the Lombard (Panther/PPC).
#
#   make -C kext syntax     # fast: compile-check vs the 10.3 IOKit headers
#   make -C kext kext       # compile + link + assemble Mach64Accelerator.kext
#   make -C kext validate   # kextload -n : validate WITHOUT loading (no risk)
#   make -C kext load       # kextload : actually load it (root; panic risk)
#
# Full kext linking uses libkmodc++ (C++ static-ctor / OSMetaClass glue).

CXX      ?= g++
CC       ?= gcc
KFRAME   := /System/Library/Frameworks/Kernel.framework
# Kernel headers first (win for IOKit/*); gcc's /usr/include supplies the arch
# base headers (ppc/ansi.h) the Kernel framework omits, so NOT -nostdinc.
KINC     := -I$(KFRAME)/Headers -I$(KFRAME)/PrivateHeaders -I. -I..
KDEF     := -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT
# -static: generate non-PIC code for kernel/static linking (no __picsymbolstub
#          sections, which `ld -static -r` rejects).
# -fapple-kext: correct C++ ABI / vtable layout for a loadable kext (gcc 3.3).
KCXX     := -static -fno-rtti -fno-exceptions -fcheck-new -fno-builtin -fapple-kext
KCC      := -static -fno-builtin

EXEC     := Mach64Accelerator
BUNDLE   := $(EXEC).kext
CPPOBJS  := Mach64Accelerator.o Mach64GLUserClient.o
OBJS     := $(CPPOBJS) kmod_glue.o

.PHONY: syntax kext validate load clean

# ---- fast header/syntax check (no codegen) --------------------------
syntax:
	@for f in $(CPPOBJS:.o=.cpp); do echo "  syntax: $$f"; \
	  $(CXX) -fsyntax-only $(KCXX) $(KDEF) $(KINC) $$f || exit 1; done
	@echo "kext sources: syntax OK"

# ---- object files ---------------------------------------------------
%.o: %.cpp
	$(CXX) -c $(KCXX) $(KDEF) $(KINC) $< -o $@
kmod_glue.o: kmod_glue.c
	$(CC) -c $(KCC) $(KDEF) $(KINC) $< -o $@

# ---- link + assemble the .kext bundle -------------------------------
$(EXEC): $(OBJS)
	ld -static -r -o $@ $(OBJS) -lkmodc++ -lcc_kext

kext: $(EXEC)
	@mkdir -p $(BUNDLE)/Contents/MacOS
	@cp Info.plist $(BUNDLE)/Contents/Info.plist
	@cp $(EXEC) $(BUNDLE)/Contents/MacOS/$(EXEC)
	@echo "built $(BUNDLE)"; file $(BUNDLE)/Contents/MacOS/$(EXEC)

# ---- validate WITHOUT loading (safe) --------------------------------
# -n: never load / no personalities to kernel   -t: run all checks
# -z: skip root-ownership auth (dev convenience; only legal without a load)
validate: kext
	kextload -n -t -z $(BUNDLE)

# ---- actually load (needs root; can panic) --------------------------
load: kext
	sudo cp -R $(BUNDLE) /tmp/ && sudo kextload -v /tmp/$(BUNDLE)

clean:
	rm -rf *.o $(EXEC) $(BUNDLE)
