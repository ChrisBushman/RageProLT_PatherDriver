# Build RageStubFB.kext on the G4 (Panther).  make syntax | make kext
CXX ?= g++
CC  ?= gcc
KFRAME := /System/Library/Frameworks/Kernel.framework
KINC := -I$(KFRAME)/Headers -I$(KFRAME)/PrivateHeaders -I.
KDEF := -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT
KCXX := -static -mlong-branch -fno-rtti -fno-exceptions -fcheck-new -fno-builtin -fapple-kext
KCC  := -static -mlong-branch -fno-builtin
EXEC := RageStubFB
BUNDLE := $(EXEC).kext
.PHONY: syntax kext clean
syntax: ; $(CXX) -fsyntax-only $(KCXX) $(KDEF) $(KINC) RageStubFB.cpp && echo syntax OK
RageStubFB.o: RageStubFB.cpp RageStubFB.h rage_modeset.h rage_modemath.h rage_modes.h rage_init_table.h ; $(CXX) -c $(KCXX) $(KDEF) $(KINC) $< -o $@
kmod_glue.o: kmod_glue.c ; $(CC) -c $(KCC) $(KDEF) $(KINC) $< -o $@
$(EXEC): RageStubFB.o kmod_glue.o ; ld -static -r -o $@ RageStubFB.o kmod_glue.o -lkmodc++ -lcc_kext
kext: $(EXEC)
	@mkdir -p $(BUNDLE)/Contents/MacOS
	@cp Info.plist $(BUNDLE)/Contents/Info.plist
	@cp $(EXEC) $(BUNDLE)/Contents/MacOS/$(EXEC)
	@echo "built $(BUNDLE)"
clean: ; rm -rf *.o $(EXEC) $(BUNDLE)
