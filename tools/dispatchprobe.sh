#!/bin/sh
# Recover the true dispatch slot->primitive mapping + per-slot calling convention.
# The driver here is built with -DGLD_DISPATCH_PROBE (probe stubs at every slot).
# sudo sh dispatchprobe.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_dispatchprobe" "$DIR/mach64_dispatchprobe.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_dispatchprobe"
cp -f /tmp/mach64gld.log "$ROOT/dispatchprobe-log.txt" 2>/dev/null
echo "=== which slot fires per primitive (correlate the unique n with r5/r6) ==="
grep -aE ">>> |DISP 0x" /tmp/mach64gld.log 2>/dev/null | head -60
kextunload /tmp/$B; echo "--- done ---"
