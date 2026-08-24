#!/bin/sh
# SOFTWARE differential run: bundle NOT installed -> Apple's software renderer.
# Break sub_4530, capture the KNOWN-GOOD populated batch + gli state.
#   sudo sh swrich.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
echo "--- removing our bundle + kext (force pure software) ---"
rm -rf "$DST"
kextunload /tmp/$B 2>/dev/null
echo "--- building mach64_swtest ---"
cc -o "$DIR/mach64_swtest" "$DIR/mach64_swtest.c" \
   -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
echo "--- gdb: richbatch @ sub_4530 (SOFTWARE) ---"
gdb -batch -x "$DIR/richbatch.gdb" "$DIR/mach64_swtest" 2>&1
echo "--- done (software) ---"
