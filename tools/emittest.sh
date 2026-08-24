#!/bin/sh
# HARDWARE differential run: our bundle + kext installed -> our renderer.
# Same emit.gdb as swrich.sh so batch/gli offsets line up for diffing.
#   sudo sh hwrich.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
echo "--- installing bundle + kext ---"
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
echo "--- building mach64_fstest ---"
cc -o "$DIR/mach64_fstest" "$DIR/mach64_fstest.c" \
   -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
echo "--- gdb: emit-fn identify @ 0x84014 ---"
gdb -batch -x "$DIR/emit.gdb" "$DIR/mach64_fstest" 2>&1
echo "--- unloading kext ---"; kextunload /tmp/$B
echo "--- done (hardware) ---"
