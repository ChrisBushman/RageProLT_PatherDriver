# Build RageDdcTest.kext on the G4 (Panther).  make syntax | make kext
CXX ?= g++
CC  ?= gcc
KFRAME := /System/Library/Frameworks/Kernel.framework
KINC := -I$(KFRAME)/Headers -I$(KFRAME)/PrivateHeaders -I.
KDEF := -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT
KCXX := -static -mlong-branch -fno-rtti -fno-exceptions -fcheck-new -fno-builtin -fapple-kext
KCC  := -static -mlong-branch -fno-builtin
EXEC := RageDdcTest
BUNDLE := $(EXEC).kext
.PHONY: syntax kext clean
syntax: ; $(CXX) -fsyntax-only $(KCXX) $(KDEF) $(KINC) RageDdcTest.cpp && echo syntax OK
RageDdcTest.o: RageDdcTest.cpp RageDdcTest.h ; $(CXX) -c $(KCXX) $(KDEF) $(KINC) $< -o $@
kmod_glue.o: kmod_glue.c ; $(CC) -c $(KCC) $(KDEF) $(KINC) $< -o $@
$(EXEC): RageDdcTest.o kmod_glue.o ; ld -static -r -o $@ RageDdcTest.o kmod_glue.o -lkmodc++ -lcc_kext
kext: $(EXEC)
	@mkdir -p $(BUNDLE)/Contents/MacOS
	@cp Info.plist $(BUNDLE)/Contents/Info.plist
	@cp $(EXEC) $(BUNDLE)/Contents/MacOS/$(EXEC)
	@echo "built $(BUNDLE)"
clean: ; rm -rf *.o $(EXEC) $(BUNDLE)
