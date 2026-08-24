#!/bin/sh
# Per-frame Z clear verification: double-buffered animated triangle at fixed depth.
# Persistent (colour-cycling) triangle = per-frame clear works; vanishes after
# frame 0 = per-frame clear broken.   sudo sh depthanim.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
cc -o "$DIR/mach64_depthanim" "$DIR/mach64_depthanim.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_depthanim"
kextunload /tmp/$B; echo "--- done ---"
