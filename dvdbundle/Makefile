# Builds ATIRageProDVDDriver.bundle on Panther (gcc 3.3). G3-safe: -mcpu=750, no AltiVec.
NAME = ATIRageProDVDDriver
CC = gcc
CFLAGS = -O2 -mcpu=750 -Wall -fno-common
BUNDLE = $(NAME).bundle
all: $(BUNDLE)
$(BUNDLE): $(NAME).c recon.c recon.h Info.plist
	rm -rf $(BUNDLE) && mkdir -p $(BUNDLE)/Contents/MacOS
	$(CC) $(CFLAGS) -bundle -o $(BUNDLE)/Contents/MacOS/$(NAME) $(NAME).c recon.c -framework ApplicationServices -framework CoreFoundation
	cp Info.plist $(BUNDLE)/Contents/Info.plist
	echo -n 'BNDL????' > $(BUNDLE)/Contents/PkgInfo
	@otool -tv $(BUNDLE)/Contents/MacOS/$(NAME) | grep -cE '\bv[a-z]+\b|\bdst\b|\bmfvscr' | sed 's/^/altivec-like insns: /'
clean:
	rm -rf $(BUNDLE)
