# gldriver/Makefile - build ATIRageProGLDriver.bundle (the OpenGL renderer plugin).
# Userspace PPC bundle; build on the Lombard.  The name MUST be ATIRageProGLDriver:
# GLEngine builds the exec path directly as /System/Library/Extensions/<name>.
# bundle/Contents/MacOS/<name>, where <name> = ATIRagePro.kext's IOGLBundleName
# (="ATIRageProGLDriver").  A different exec name -> silent dlopen fail.
CC     ?= cc
# GLD_APPLE_PROBE is now the MAIN Path-B path (not a probe): gld_context.c opens Apple's
# ATIRagePro GL client directly and gldAttachDrawable creates/binds the surface.
# GLD_DBUF enables double-buffered pixel formats (node[0x0c] bit3) -- WITHOUT it the CGL
# scorer rejects every DB request (npix=0), and since all real apps ask for double buffering
# they fall back to software (white).  Both flags are mandatory for the Path-B bundle.
CFLAGS ?= -O2 -Wall -std=gnu99 -mcpu=750 -fsingle-precision-constant -I.. -DGLD_APPLE_PROBE -DGLD_DBUF

EXEC   := ATIRageProGLDriver
BUNDLE := $(EXEC).bundle
SRC    := gld_glue.c gld_context.c gld_state.c gld_render.c gld_prims.c \
          gld_texture.c gld_dispatch.c gld_pixelformat.c gld_entry.c gld_cgs.c gld_engine.c \
          ../common/mach64_emit.c

.PHONY: bundle clean
bundle: $(BUNDLE)

$(EXEC): $(SRC) gld.h ../common/mach64_emit.h ../common/mach64_regs.h ../common/mach64_abi.h ../common/atiragepro_abi.h
	$(CC) $(CFLAGS) -bundle -o $(EXEC) $(SRC) -framework IOKit -framework ApplicationServices -framework CoreFoundation

$(BUNDLE): $(EXEC) Info.plist
	@mkdir -p $(BUNDLE)/Contents/MacOS
	@cp Info.plist $(BUNDLE)/Contents/Info.plist
	@cp $(EXEC) $(BUNDLE)/Contents/MacOS/$(EXEC)
	@echo "built $(BUNDLE)"; file $(BUNDLE)/Contents/MacOS/$(EXEC)
	@echo "exported gld* symbols:"; nm -g $(BUNDLE)/Contents/MacOS/$(EXEC) 2>/dev/null | grep -c ' T _gld'

clean:
	rm -rf $(EXEC) $(BUNDLE)
