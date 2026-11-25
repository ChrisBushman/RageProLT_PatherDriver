# kext/Makefile - build ATIRageProLT.kext on a Panther PPC host (the G4 rig).  Nothing here loads the kext.
#   make syntax    compile-check against the 10.3 kernel headers
#   make kext      compile + link + assemble the bundle
#   make checkfp   fail if the kext contains ANY floating-point instruction (design 2: no FPU in the kernel, no double anywhere)
#   make validate  kextload -n  (validate only, does NOT load)
CXX ?= g++
CC  ?= gcc
KFRAME := /System/Library/Frameworks/Kernel.framework
KINC := -I$(KFRAME)/Headers -I$(KFRAME)/PrivateHeaders -I. -I../common -I../config
KDEF := -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT
# -mcpu=750 -msoft-float: G3 target, no FP instructions at all.  Other flags are the proven set from the earlier kext.
EXTRA ?=
KARCH := -mcpu=750 -msoft-float $(EXTRA)   # EXTRA: e.g. -DRAGE_DEBUG_PANIC=1 for the forced-panic test build only
KCXX := -static -mlong-branch -fno-rtti -fno-exceptions -fcheck-new -fno-builtin -fapple-kext $(KARCH) -O2 -Wall
KCC  := -static -mlong-branch -fno-builtin $(KARCH) -O2 -Wall
EXEC := ATIRageProLT
BUNDLE := $(EXEC).kext
OBJS := RageLTDevice.o RageLTLogClient.o RageLTSelfTest.o RageLTEngine.o RageLTMode.o RageLTDma.o RageLTGLClient.o kmod_glue.o

.PHONY: syntax kext checkfp validate clean
syntax:
	$(CXX) -fsyntax-only $(KCXX) $(KDEF) $(KINC) RageLTDevice.cpp
	$(CXX) -fsyntax-only $(KCXX) $(KDEF) $(KINC) RageLTLogClient.cpp
	$(CXX) -fsyntax-only $(KCXX) $(KDEF) $(KINC) RageLTSelfTest.cpp
%.o: %.cpp
	$(CXX) -c $(KCXX) $(KDEF) $(KINC) $< -o $@
kmod_glue.o: kmod_glue.c
	$(CC) -c $(KCC) $(KDEF) $(KINC) $< -o $@
$(EXEC): $(OBJS)
	ld -static -r -o $@ $(OBJS) -lkmodc++ -lcc_kext
kext: $(EXEC)
	@mkdir -p $(BUNDLE)/Contents/MacOS
	cp Info.plist $(BUNDLE)/Contents/Info.plist
	cp $(EXEC) $(BUNDLE)/Contents/MacOS/$(EXEC)
	@echo built $(BUNDLE)
# FP mnemonics: every PPC instruction that starts with f, plus lf*/stf* loads and stores, and mffs/mtfsf etc.  Run on the final object.
checkfp: $(EXEC)
	@otool -tv $(EXEC) | awk '{ for (i = 1; i <= NF; i++) if ($$i ~ /^(f[a-z0-9.]+|lf[sd]u?x?|stf[sd]u?x?|stfiwx|mffs|mtfsf|mtfsb[01]|mcrfs)$$/) { print; found = 1; break } } END { exit found }' \
	  && echo "checkfp: no floating-point instructions in $(EXEC)" \
	  || { echo "checkfp: FLOATING-POINT INSTRUCTIONS FOUND (listed above)"; exit 1; }
validate: kext
	kextload -n -t $(BUNDLE)
clean:
	rm -rf $(OBJS) $(EXEC) $(BUNDLE)
