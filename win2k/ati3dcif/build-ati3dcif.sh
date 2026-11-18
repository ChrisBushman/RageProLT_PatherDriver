#!/bin/sh
# Build our ati3dcif.dll for Windows 2000 / Rage Mobility-P.
#
# CRT-free (kernel32/user32 only) so it always loads on Win2000 -- same recipe as the
# winsys tools (build-winsys.sh), just linked as a DLL with a custom entry + .def.
# Drives the mach64 through win2k/driver/mach64_winsys.c + common/mach64_emit.c.
#
# Deploy beside the SDK demos (they LoadLibraryA("ati3dcif.dll") from the exe's dir):
#   scp ati3dcif.dll armada:ragesdk/bin/
#   (needs win2k/kmd/mach64kmd.sys started for real geometry; Increment 1 traces even
#    without it -> C:\ati3dcif.log)
set -e
cd "$(dirname "$0")"
GCC=i686-w64-mingw32-gcc

$GCC -shared -O2 -w -D_WIN32_WINNT=0x0500 \
  -nostartfiles -nodefaultlibs \
  -Wl,-e,_DllMainCRTStartup@12 -Wl,--kill-at -Wl,--enable-stdcall-fixup \
  -I. -I../driver -I../../common \
  -o ati3dcif.dll \
  ati3dcif.c ../driver/mach64_winsys.c ../../common/mach64_emit.c \
  ati3dcif.def \
  -lkernel32 -luser32 -lgcc

echo "built ati3dcif.dll"
i686-w64-mingw32-objdump -p ati3dcif.dll 2>/dev/null | sed -n '/Export/,/^$/p' | head -50
