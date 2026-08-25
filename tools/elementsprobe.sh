#!/bin/sh
# Discover the glDrawElements (indexed) dispatch path.  Requires the driver built
# with -DGLD_DISPATCH_PROBE (probe stubs at slots 0x18..0x48).
# sudo sh elementsprobe.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_elementsprobe" "$DIR/mach64_elementsprobe.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_elementsprobe" 2>&1 | tee /tmp/elemtest.out
cp -f /tmp/elemtest.out    "$ROOT/elementsprobe-testout.txt" 2>/dev/null
cp -f /tmp/mach64gld.log   "$ROOT/elementsprobe-log.txt"     2>/dev/null
echo "=== VBUF/IDXBUF (from test) then which slot fires per indexed primitive ==="
grep -aE "VBUF=" /tmp/elemtest.out 2>/dev/null
grep -aE "DISP 0x" /tmp/mach64gld.log 2>/dev/null | head -60
kextunload /tmp/$B; echo "--- done ---"
