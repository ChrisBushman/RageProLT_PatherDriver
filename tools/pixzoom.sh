#!/bin/sh
# glPixelZoom + GL_UNPACK_SWAP_BYTES for glDrawPixels.  Installs bundle+kext, runs mach64_pixzoom.
# PASS: z1 = pure colours; z4 = same colours (each source pixel is a 4x4 block), z4 past-edge = blue;
# swap = (64,48,32).  The driver "glDrawPixels: ... zoom=X,Y" log confirms the zoom offset read.
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
rm -rf "$ROOT/gldriver/ATIRageProGLDriver.bundle"; rm -f "$DIR/mach64_pixzoom"
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF" bundle ) || { echo "BUNDLE BUILD FAILED"; exit 1; }
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$ROOT/gldriver/ATIRageProGLDriver.bundle" "$DST"; chown -R root:wheel "$DST"
kextunload /tmp/$B 2>/dev/null
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_pixzoom" "$DIR/mach64_pixzoom.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 | grep -viE "warning:" ; true
[ -x "$DIR/mach64_pixzoom" ] || { echo "TEST BUILD FAILED"; kextunload /tmp/$B; exit 1; }
RESULT="$ROOT/pixzoom-out.txt"
"$DIR/mach64_pixzoom" 2>&1 | tee "$RESULT"
kextunload /tmp/$B
echo "=== driver glDrawPixels log (zoom=X,Y confirms the gc offset) ===" | tee -a "$RESULT"
grep -aE "glDrawPixels:" /tmp/mach64gld.log | tail -4 | tee -a "$RESULT"
echo "--- done; output in pixzoom-out.txt ---"
