#!/bin/sh
# Test the double-buffer dispatch gate.  Rebuilds the renderer bundle with -DGLD_DBUF
# (offers a real AGL_DOUBLEBUFFER pixel format so the context attaches as type 0x36
# and gldAttachDrawable sets the DB gli flag), installs it, runs mach64_dbgate.
# PASS = an RGB triangle on black (GLEngine dispatched primitives).
# FAIL = black screen (still routed to no-op slot 0x60).  Run on the Lombard, sudo.
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF" bundle ) || exit 1
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_dbgate" "$DIR/mach64_dbgate.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_dbgate"
kextunload /tmp/$B
echo "=== attach/gate/dispatch log ==="
grep -aE "DB-gate|AttachDrawable|type=0x36|dbuf|slot|render|present" /tmp/mach64gld.log | head -30
echo "--- done ---"
