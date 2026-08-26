#!/bin/sh
# Double-buffer swap-chain test.  Rebuilds the renderer bundle with -DGLD_DBUF (real
# AGL_DOUBLEBUFFER), installs it, runs mach64_dbanim (RGB triangle sweeping across many
# aglSwapBuffers).  WATCH it: smooth motion = swap chain OK.  Snapshot at the end should
# show ONE clean centered triangle on black.  Run on the Lombard under sudo.
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF" bundle ) || exit 1
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_dbanim" "$DIR/mach64_dbanim.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_dbanim"
kextunload /tmp/$B
echo "=== attach/gate/present log (first + swap sample) ==="
grep -aE "DB-gate|type=0x36|present|DOUBLEBUFFER" /tmp/mach64gld.log | head -8
echo "--- done ---"
