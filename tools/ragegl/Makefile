# Build on the Panther host: make -C tools/ragegl
CFLAGS ?= -O2 -Wall -I../../common
ragegl_test: ragegl_test.c ragegl.h
	cc $(CFLAGS) -o $@ ragegl_test.c -framework IOKit -framework CoreFoundation
clean:
	rm -f ragegl_test
