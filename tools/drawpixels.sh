#!/bin/sh
# Verify glDrawPixels(GL_RGBA/GL_UNSIGNED_BYTE).  Rebuilds the GL bundle (mach64_ctl0c = the upload)
# + kext, installs, runs mach64_drawpixels (upload a 64x64 red|green/blue image -> glReadPixels check).
# Run on the Lombard under sudo.  Force-clears stale (root-owned) artifacts so an old binary can't run.
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
rm -rf "$ROOT/gldriver/ATIRageProGLDriver.bundle"
rm -f  "$DIR/mach64_drawpixels"
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF" bundle ) || { echo "BUNDLE BUILD FAILED"; exit 1; }
[ -d "$ROOT/gldriver/ATIRageProGLDriver.bundle" ] || { echo "BUNDLE NOT BUILT"; exit 1; }
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$ROOT/gldriver/ATIRageProGLDriver.bundle" "$DST"; chown -R root:wheel "$DST"
kextunload /tmp/$B 2>/dev/null
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_drawpixels" "$DIR/mach64_drawpixels.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 | grep -viE "warning:" ; true
[ -x "$DIR/mach64_drawpixels" ] || { echo "TEST BUILD FAILED"; kextunload /tmp/$B; exit 1; }
echo "test binary: `ls -l "$DIR/mach64_drawpixels" | awk '{print $5, $6, $7, $8}'`"
RESULT="$ROOT/drawpixels-out.txt"
"$DIR/mach64_drawpixels" 2>&1 | tee "$RESULT"
kextunload /tmp/$B
echo "=== glDrawPixels log ===" | tee -a "$RESULT"
grep -a "glDrawPixels" /tmp/mach64gld.log | tail -4 | tee -a "$RESULT"
echo "--- done; output in drawpixels-out.txt ---"
