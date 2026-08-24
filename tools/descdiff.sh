#!/bin/sh
# Dump GLEngine's drawable descriptor gli[0x789c] in HW + SW for comparison.
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
echo "=== [1/2] HARDWARE descriptor ==="
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
cc -o "$DIR/mach64_fstest" "$DIR/mach64_fstest.c" -framework Carbon -framework AGL -framework OpenGL 2>/dev/null
gdb -batch -x "$DIR/descdump.gdb" "$DIR/mach64_fstest" 2>&1 | grep -E "GLI=|desc\[|WIDTH"
kextunload /tmp/$B 2>/dev/null
echo "=== [2/2] SOFTWARE descriptor ==="
rm -rf "$DST"
cc -o "$DIR/mach64_swtest" "$DIR/mach64_swtest.c" -framework Carbon -framework AGL -framework OpenGL 2>/dev/null
gdb -batch -x "$DIR/descdump.gdb" "$DIR/mach64_swtest" 2>&1 | grep -E "GLI=|desc\[|WIDTH"
echo "=== done ==="
