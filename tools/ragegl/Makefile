# Build on the Panther host: make -C tools/ragegl
CFLAGS ?= -O2 -Wall -I../../common
BUNDLE_SRC = ../../gldriver/gld_core.c ../../gldriver/gld_context.c ../../gldriver/gld_entry.c ../../gldriver/gld_prims.c ../../gldriver/gld_dispatch.c ../../gldriver/gld_pixelformat.c ../../gldriver/gld_cgs.c ../../gldriver/gld_glue.c ../../gldriver/gld_pixels.c ../../gldriver/gld_texture.c ../../gldriver/rage_backend.c ../../common/mach64_emit.c
all: ragegl_test ragegl_bench gldcore_test bundle_test
ragegl_test: ragegl_test.c ragegl.h
	cc $(CFLAGS) -o $@ ragegl_test.c -framework IOKit -framework CoreFoundation
ragegl_bench: ragegl_bench.c ragegl.h
	cc $(CFLAGS) -o $@ ragegl_bench.c -framework IOKit -framework CoreFoundation
gldcore_test: gldcore_test.c ../../gldriver/rage_backend.c ../../gldriver/rage_backend.h ../../common/mach64_emit.c
	cc $(CFLAGS) -I../../gldriver -std=gnu99 -o $@ gldcore_test.c ../../gldriver/rage_backend.c ../../common/mach64_emit.c -framework IOKit -framework CoreFoundation
bundle_test: bundle_test.c $(BUNDLE_SRC) ../../gldriver/gld.h
	cc $(CFLAGS) -I../../gldriver -std=gnu99 -o $@ bundle_test.c $(BUNDLE_SRC) -framework IOKit -framework CoreFoundation -framework ApplicationServices
clean:
	rm -f ragegl_test ragegl_bench gldcore_test bundle_test
