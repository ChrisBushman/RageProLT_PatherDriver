#!/bin/sh
# Find the GL_UNPACK_* offsets in GLEngine's gc.  Installs the driver bundle+kext (so GL_RENDERER is
# our accelerated driver), runs mach64_unpackprobe (sets UNPACK sentinels, scans the gc pixel-store
# region).  Pure userspace reads -> safe.  Run in the console session: sudo sh tools/unpackprobe.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
rm -rf "$ROOT/gldriver/ATIRageProGLDriver.bundle"; rm -f "$DIR/mach64_unpackprobe"
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF" bundle ) || { echo "BUNDLE BUILD FAILED"; exit 1; }
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$ROOT/gldriver/ATIRageProGLDriver.bundle" "$DST"; chown -R root:wheel "$DST"
( cd "$ROOT/kext" && make kext ) >/dev/null 2>&1 || { echo "KEXT BUILD FAILED"; exit 1; }
kextunload /tmp/$B 2>/dev/null
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
cc -O2 -o "$DIR/mach64_unpackprobe" "$DIR/mach64_unpackprobe.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 | grep -viE "warning:" ; true
[ -x "$DIR/mach64_unpackprobe" ] || { echo "TEST BUILD FAILED"; kextunload /tmp/$B; exit 1; }
"$DIR/mach64_unpackprobe" 2>&1 | tee "$ROOT/unpackprobe-out.txt"
kextunload /tmp/$B
echo "--- done; output in unpackprobe-out.txt ---"
