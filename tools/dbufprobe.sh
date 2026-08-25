#!/bin/sh
# Investigation: does our pixel format yield a double-buffered context, and does
# aglSwapBuffers drive gldFlushBuffer?  sudo sh dbufprobe.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_dbufprobe" "$DIR/mach64_dbufprobe.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_dbufprobe" 2>&1 | tee /tmp/dbuftest.out
cp -f /tmp/dbuftest.out  "$ROOT/dbufprobe-testout.txt" 2>/dev/null
cp -f /tmp/mach64gld.log "$ROOT/dbufprobe-log.txt"     2>/dev/null
echo "=== test output ==="; grep -aE 'DOUBLEBUFFER|RENDERER|>>> |no ' /tmp/dbuftest.out 2>/dev/null
echo "=== driver: attach + flushbuffer calls ==="
grep -aE 'gldFlushBuffer|gldAttachDrawable|framebuffer CRTC|cbpp' /tmp/mach64gld.log 2>/dev/null | head -20
kextunload /tmp/$B; echo "--- done ---"
