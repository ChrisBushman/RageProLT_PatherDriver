# Build on the Panther host: make -C tools/ragegl
CFLAGS ?= -O2 -Wall -I../../common
all: ragegl_test ragegl_bench gldcore_test
ragegl_test: ragegl_test.c ragegl.h
	cc $(CFLAGS) -o $@ ragegl_test.c -framework IOKit -framework CoreFoundation
ragegl_bench: ragegl_bench.c ragegl.h
	cc $(CFLAGS) -o $@ ragegl_bench.c -framework IOKit -framework CoreFoundation
gldcore_test: gldcore_test.c ../../gldriver/rage_backend.c ../../gldriver/rage_backend.h ../../common/mach64_emit.c
	cc $(CFLAGS) -I../../gldriver -std=gnu99 -o $@ gldcore_test.c ../../gldriver/rage_backend.c ../../common/mach64_emit.c -framework IOKit -framework CoreFoundation
clean:
	rm -f ragegl_test ragegl_bench gldcore_test
