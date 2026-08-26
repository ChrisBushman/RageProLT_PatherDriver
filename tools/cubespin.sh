#!/bin/sh
# Animated textured/depth cube (single-buffered): validates the per-frame COLOUR + DEPTH
# clears under motion.  PASS = a clean spinning cube; smearing/trails = colour clear failing,
# flicker/Z-fight/far-faces-punching-through = depth clear failing.  Some flicker/tear is
# normal (single-buffered).  Run on the Lombard under sudo:  sudo sh tools/cubespin.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; make bundle ) || exit 1
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_cubespin" "$DIR/mach64_cubespin.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_cubespin"
kextunload /tmp/$B
cp -f /tmp/mach64gld.log "$ROOT/cubespin-log.txt" 2>/dev/null
echo "=== watch: a clean spinning cube (no smearing = colour clear OK, no Z-fight = depth clear OK) ==="
echo "--- done (log in cubespin-log.txt) ---"
