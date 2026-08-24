#!/bin/sh
# HARDWARE run: our bundle + kext installed -> our renderer.  Live glDrawArrays
# call trace (drawtrace.gdb).  Writes the trace to $ROOT/drawtrace-output.txt.
#   sudo sh drawtracetest.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
OUT="$ROOT/drawtrace-output.txt"
echo "--- installing bundle + kext ---"
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
echo "--- building mach64_fstest ---"
cc -o "$DIR/mach64_fstest" "$DIR/mach64_fstest.c" \
   -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
echo "--- gdb: live glDrawArrays call trace (also -> $OUT) ---"
gdb -batch -x "$DIR/drawtrace.gdb" "$DIR/mach64_fstest" 2>&1 | tee "$OUT"
echo "--- unloading kext ---"; kextunload /tmp/$B
echo "--- wrote $OUT ---"
echo "--- done (hardware) ---"
