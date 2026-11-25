# gldriver/Makefile - build ATIRageProGLDriver.bundle, the OpenGL renderer plugin for the standalone Rage LT Pro driver.
# Userspace PPC bundle; build on the Panther host (or the G5).  GLEngine builds the executable path from the kext's IOGLBundleName, so EXEC must match it.
# The bundle talks to the kernel only through the RageLTGLClient selectors (rage_backend.c); it has no CPU view of VRAM.
CC     ?= cc
CFLAGS ?= -O2 -Wall -std=gnu99 -mcpu=750 -fsingle-precision-constant -DGLD_DBUF -I. -I../common   # GLD_DBUF: advertise double-buffered pixel formats (the CGL scorer rejects every DB request without it)

EXEC   := ATIRageProGLDriver
BUNDLE := $(EXEC).bundle
SRC    := gld_core.c gld_context.c gld_entry.c gld_prims.c gld_dispatch.c gld_pixelformat.c gld_cgs.c gld_glue.c gld_pixels.c gld_texture.c gld_stencil.c rage_backend.c \
          ../common/mach64_emit.c

.PHONY: bundle clean
bundle: $(BUNDLE)

$(EXEC): $(SRC) gld.h rage_backend.h ../common/mach64_emit.h ../common/mach64_regs.h ../common/rage_abi.h ../common/rage_vmath.h
	$(CC) $(CFLAGS) -bundle -o $(EXEC) $(SRC) -framework IOKit -framework ApplicationServices -framework CoreFoundation

$(BUNDLE): $(EXEC) Info.plist
	@mkdir -p $(BUNDLE)/Contents/MacOS
	@cp Info.plist $(BUNDLE)/Contents/Info.plist
	@cp $(EXEC) $(BUNDLE)/Contents/MacOS/$(EXEC)
	@echo "built $(BUNDLE)"; file $(BUNDLE)/Contents/MacOS/$(EXEC)
	@echo "exported gld* symbols:"; nm -g $(BUNDLE)/Contents/MacOS/$(EXEC) 2>/dev/null | grep ' T _gld'

clean:
	rm -rf $(EXEC) $(BUNDLE)
