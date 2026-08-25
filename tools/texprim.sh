#!/bin/sh
# Verify TRIANGLE_STRIP/FAN/QUADS dispatch+decomposition (slot labels off by one).
# Same four-quadrant texture drawn 4 ways; each should be an identical clean square.
# sudo sh texprim.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_texprim" "$DIR/mach64_texprim.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_texprim"
cp -f /tmp/mach64gld.log "$ROOT/texprim-log.txt" 2>/dev/null
echo "=== which render path + decomposition per primitive ==="
grep -aE "PRIM |^D[0-9]" /tmp/mach64gld.log 2>/dev/null | head -30
kextunload /tmp/$B; echo "--- done ---"
