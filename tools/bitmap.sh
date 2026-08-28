#!/bin/sh
# Verify glBitmap.  Rebuilds the GL bundle (mach64_ctl14 = bitmap draw) + kext, installs, runs
# mach64_bitmap (bg quad -> yellow raster -> 16x16 mask, bottom-left 8x8 set -> glReadPixels check).
# Force-clears stale (root-owned) artifacts.  Run on the Lombard under sudo.
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
rm -rf "$ROOT/gldriver/ATIRageProGLDriver.bundle"
rm -f  "$DIR/mach64_bitmap"
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF" bundle ) || { echo "BUNDLE BUILD FAILED"; exit 1; }
[ -d "$ROOT/gldriver/ATIRageProGLDriver.bundle" ] || { echo "BUNDLE NOT BUILT"; exit 1; }
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$ROOT/gldriver/ATIRageProGLDriver.bundle" "$DST"; chown -R root:wheel "$DST"
kextunload /tmp/$B 2>/dev/null
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_bitmap" "$DIR/mach64_bitmap.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 | grep -viE "warning:" ; true
[ -x "$DIR/mach64_bitmap" ] || { echo "TEST BUILD FAILED"; kextunload /tmp/$B; exit 1; }
echo "test binary: `ls -l "$DIR/mach64_bitmap" | awk '{print $5, $6, $7, $8}'`"
RESULT="$ROOT/bitmap-out.txt"
"$DIR/mach64_bitmap" 2>&1 | tee "$RESULT"
kextunload /tmp/$B
echo "=== glBitmap log ===" | tee -a "$RESULT"
grep -a "glBitmap" /tmp/mach64gld.log | tail -4 | tee -a "$RESULT"
echo "--- done; output in bitmap-out.txt ---"
