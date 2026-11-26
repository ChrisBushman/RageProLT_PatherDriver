# Build on the Panther host: make -C tools/ragegl
CFLAGS ?= -O2 -Wall -I../../common
BUNDLE_SRC = ../../gldriver/gld_core.c ../../gldriver/gld_context.c ../../gldriver/gld_entry.c ../../gldriver/gld_prims.c ../../gldriver/gld_dispatch.c ../../gldriver/gld_pixelformat.c ../../gldriver/gld_cgs.c ../../gldriver/gld_glue.c ../../gldriver/gld_pixels.c ../../gldriver/gld_texture.c ../../gldriver/gld_stencil.c ../../gldriver/rage_backend.c ../../common/mach64_emit.c
all: scanshot ragegl_test depth32_test ragegl_bench xfer_test gldcore_test bundle_test gl_suite
ragegl_test: ragegl_test.c ragegl.h
	cc $(CFLAGS) -o $@ ragegl_test.c -framework IOKit -framework CoreFoundation
depth32_test: depth32_test.c ragegl.h
	cc $(CFLAGS) -o $@ depth32_test.c -framework IOKit -framework CoreFoundation
xfer_test: xfer_test.c ragegl.h
	cc $(CFLAGS) -o $@ xfer_test.c -framework IOKit -framework CoreFoundation
ragegl_bench: ragegl_bench.c ragegl.h
	cc $(CFLAGS) -o $@ ragegl_bench.c -framework IOKit -framework CoreFoundation
gldcore_test: gldcore_test.c ../../gldriver/rage_backend.c ../../gldriver/rage_backend.h ../../common/mach64_emit.c
	cc $(CFLAGS) -I../../gldriver -std=gnu99 -o $@ gldcore_test.c ../../gldriver/rage_backend.c ../../common/mach64_emit.c -framework IOKit -framework CoreFoundation
bundle_test: bundle_test.c $(BUNDLE_SRC) ../../gldriver/gld.h
	cc $(CFLAGS) -I../../gldriver -std=gnu99 -o $@ bundle_test.c $(BUNDLE_SRC) -framework IOKit -framework CoreFoundation -framework ApplicationServices
gl_suite: ../../tests/suite/gl_suite.c $(BUNDLE_SRC) ../../gldriver/gld.h
	cc $(CFLAGS) -I../../gldriver -std=gnu99 -o $@ ../../tests/suite/gl_suite.c $(BUNDLE_SRC) -framework IOKit -framework CoreFoundation -framework ApplicationServices
scanshot: scanshot.c ../../gldriver/rage_backend.c ../../gldriver/rage_backend.h
	cc $(CFLAGS) -I../../gldriver -std=gnu99 -o $@ scanshot.c ../../gldriver/rage_backend.c -framework IOKit -framework CoreFoundation
clean:
	rm -f scanshot ragegl_test depth32_test ragegl_bench xfer_test gldcore_test bundle_test gl_suite
