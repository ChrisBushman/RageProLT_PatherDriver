# win2k/kmd - Rage Mobility-P Win2000 kernel helper + user test tool.
# Cross-built from macOS with MinGW-w64 (ships ddk/ntddk.h + libntoskrnl/libhal).
# Win2000 loads unsigned drivers, so no signing step.
#
#   make            build mach64kmd.sys + kmdtest.exe
#   make deploy     scp both to the Armada (ssh alias 'armada')
#   make test       install + start + read CONFIG_CHIP_ID on the Armada
#   make clean

CC      := i686-w64-mingw32-gcc
OBJDUMP := i686-w64-mingw32-objdump
# ddk/ subdir holds wdm.h that ntddk.h includes as <wdm.h>.
# Derive the target sysroot from the ntoskrnl import lib: <sysroot>/lib/libntoskrnl.a
DDKINC  := $(dir $(shell $(CC) -print-file-name=libntoskrnl.a))../include/ddk

# --- kernel driver: native subsystem, DriverEntry entry, no CRT ---
DRV_CFLAGS := -O2 -Wall -fno-stack-protector -mno-stack-arg-probe -ffreestanding \
              -nostdlib -D_X86_ -I$(DDKINC)
DRV_LDFLAGS := -Wl,--subsystem,native -Wl,--entry,_DriverEntry@8 \
               -Wl,--image-base,0x10000 -shared -nostdlib
DRV_LIBS := -lntoskrnl -lhal

# --- user tool: CRT-free so it loads on Win2000 (no EncodePointer import) ---
TOOL_CFLAGS := -O2 -Wall -D_WIN32_WINNT=0x0500 -nostartfiles -nodefaultlibs -e _mainstart
TOOL_LIBS := -lkernel32 -ladvapi32 -luser32

HOST    := armada
SYSPATH := C:\\mach64kmd.sys

all: mach64kmd.sys kmdtest.exe

mach64kmd.sys: mach64kmd.c
	$(CC) $(DRV_CFLAGS) $(DRV_LDFLAGS) -o $@ $< $(DRV_LIBS)
	@$(OBJDUMP) -p $@ | grep -iE 'Subsystem|DLL Name' || true

kmdtest.exe: kmdtest.c
	$(CC) $(TOOL_CFLAGS) -o $@ $< $(TOOL_LIBS)

deploy: all
	scp mach64kmd.sys $(HOST):/cygdrive/c/mach64kmd.sys
	scp kmdtest.exe   $(HOST):/home/Doc/kmdtest.exe

test: deploy
	ssh $(HOST) "cd /home/Doc && ./kmdtest.exe auto '$(SYSPATH)'"

clean:
	rm -f mach64kmd.sys kmdtest.exe
