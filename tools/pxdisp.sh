#!/bin/sh
# Pin the GLEngine dispatch impls for glDrawPixels/glCopyPixels/glBitmap (docs/re-notes/drawpixels.md).
# Installs the driver bundle+kext (so GL_RENDERER is our accelerated driver), runs mach64_pxdisp,
# which reads the per-context pixel-op dispatch slots ([ctx+0x114]/[ctx+0xd4]/[ctx+0x1c]/[ctx+0x3b8])
# and dladdr's them -> GLEngine offsets to disassemble.  Pure userspace reads -> safe.  Console:
#   sudo sh tools/pxdisp.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF" bundle ) || exit 1
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$ROOT/gldriver/ATIRageProGLDriver.bundle" "$DST"; chown -R root:wheel "$DST"
( cd "$ROOT/kext" && make kext ) || { echo "KEXT BUILD FAILED"; exit 1; }
kextunload /tmp/$B 2>/dev/null
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
cc -O2 -o "$DIR/mach64_pxdisp" "$DIR/mach64_pxdisp.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 | grep -viE "warning:" ; true
[ -x "$DIR/mach64_pxdisp" ] || { echo "BUILD FAILED"; kextunload /tmp/$B; exit 1; }
echo "=== running pxdisp (reads pixel-op dispatch slots) ==="
"$DIR/mach64_pxdisp" 2>&1 | tee "$ROOT/pxdisp-out.txt"
kextunload /tmp/$B
echo "--- done; output in pxdisp-out.txt ---"
