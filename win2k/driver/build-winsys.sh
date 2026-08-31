#!/bin/sh
# Build the standalone winsys library test (proves mach64_winsys.c drives the HW).
# Needs win2k/kmd/mach64kmd.sys loaded on the target. CRT-free.
set -e
cd "$(dirname "$0")"
i686-w64-mingw32-gcc -O2 -w -D_WIN32_WINNT=0x0500 -nostartfiles -nodefaultlibs -e _mainstart \
  -I../../common -I. \
  -o mach64_winsys_test.exe mach64_winsys_test.c mach64_winsys.c ../../common/mach64_emit.c \
  -lkernel32 -luser32 -lgcc
echo "built mach64_winsys_test.exe"
echo "deploy+run: scp mach64_winsys_test.exe armada:/home/Doc/ && ssh armada 'cd /home/Doc && ./mach64_winsys_test.exe'"
i686-w64-mingw32-gcc -O2 -w -D_WIN32_WINNT=0x0500 -nostartfiles -nodefaultlibs -e _mainstart \
  -I../../common -I. \
  -o mach64_texproof.exe mach64_texproof.c mach64_winsys.c ../../common/mach64_emit.c \
  -lkernel32 -luser32 -lgcc
echo "built mach64_texproof.exe"
i686-w64-mingw32-gcc -O2 -w -D_WIN32_WINNT=0x0500 -nostartfiles -nodefaultlibs -e _mainstart \
  -I../../common -I. \
  -o mach64_depthproof.exe mach64_depthproof.c mach64_winsys.c ../../common/mach64_emit.c \
  -lkernel32 -luser32 -lgcc
echo "built mach64_depthproof.exe"
