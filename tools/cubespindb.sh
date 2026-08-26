#!/bin/sh
# Double-buffered animated textured/depth cube.  Rebuilds the renderer bundle with -DGLD_DBUF
# (enables the AGL_DOUBLEBUFFER pixel format) so the cube renders off-screen and page-flips a
# complete frame each swap -- no single-buffered tearing.  PASS = a smooth spinning solid
# cube, correctly occluded, no flicker/tearing.  Run on the Lombard:  sudo sh tools/cubespindb.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF" bundle ) || exit 1
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_cubespindb" "$DIR/mach64_cubespindb.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_cubespindb" 2>&1 | tee "$ROOT/cubespindb-frames.txt"
kextunload /tmp/$B
cp -f /tmp/mach64gld.log "$ROOT/cubespindb-log.txt" 2>/dev/null
echo "=== watch: a smooth, solid, correctly-occluded spinning cube (no tearing) ==="
echo "--- done (log in cubespindb-log.txt) ---"
