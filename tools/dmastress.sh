#!/bin/sh
# Stress-test the DMA batch-split fix: a 40x30 gouraud grid (2400 triangles -> ~73 submits).
# Default build.  PASS = the whole screen is one smooth gradient (dark bottom-left, red
# bottom-right, green top-left, yellow top-right) with no missing rectangles and no colour
# banding.  Missing region = still truncating; a wrong-colour band = a post-split batch lost
# its render state.  Run on the Lombard under sudo:  sudo sh tools/dmastress.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; make bundle ) || exit 1
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_dmastress" "$DIR/mach64_dmastress.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_dmastress"
kextunload /tmp/$B
cp -f /tmp/mach64gld.log "$ROOT/dmastress-log.txt" 2>/dev/null
echo "=== screenshot: full-screen smooth gradient, no gaps, no colour bands ==="
echo "--- done (log in dmastress-log.txt) ---"
