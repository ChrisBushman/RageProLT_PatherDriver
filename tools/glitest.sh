#!/bin/sh
# Install bundle + kext, run the accelerated CGL app under gdb tracing
# cglScorePixelFormat -> see which nodes are scored and what each returns.
#   sudo sh scoretest.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
echo "--- installing bundle + kext ---"
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
echo "--- gdb: trace gliChoosePixelFormat renderers ---"
gdb -batch -x "$DIR/gli.gdb" "$DIR/mach64_gltest" 2>&1
echo "--- unloading kext ---"; kextunload /tmp/$B
echo "--- done ---"
