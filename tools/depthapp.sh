#!/bin/sh
# Depth occlusion demo: install driver + kext, build+run the two-overlapping-
# triangle app (near green drawn first, far red second).  Overlap GREEN = depth works.
#   sudo sh depthapp.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
echo "--- install bundle + kext ---"
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
echo "--- build + run depthapp ---"
cc -o "$DIR/mach64_depthapp" "$DIR/mach64_depthapp.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_depthapp"
echo "--- /tmp/mach64gld.log (tail) ---"; tail -20 /tmp/mach64gld.log 2>/dev/null
kextunload /tmp/$B; echo "--- done ---"
