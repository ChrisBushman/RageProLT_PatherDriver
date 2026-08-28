#!/bin/sh
# Verify bilinear texture filtering + find the MIN/MAG filter offset.  Builds the GL bundle with
# (real bilinear filtering) + kext, installs, runs
# mach64_texfilter (2x2 texture magnified, reads the centre).  Run on the Lombard under sudo.
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
rm -rf "$ROOT/gldriver/ATIRageProGLDriver.bundle"; rm -f "$DIR/mach64_texfilter"
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF" bundle ) \
  || { echo "BUNDLE BUILD FAILED"; exit 1; }
[ -d "$ROOT/gldriver/ATIRageProGLDriver.bundle" ] || { echo "BUNDLE NOT BUILT"; exit 1; }
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$ROOT/gldriver/ATIRageProGLDriver.bundle" "$DST"; chown -R root:wheel "$DST"
kextunload /tmp/$B 2>/dev/null
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_texfilter" "$DIR/mach64_texfilter.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 | grep -viE "warning:" ; true
[ -x "$DIR/mach64_texfilter" ] || { echo "TEST BUILD FAILED"; kextunload /tmp/$B; exit 1; }
RESULT="$ROOT/texfilter-out.txt"
"$DIR/mach64_texfilter" 2>&1 | tee "$RESULT"
kextunload /tmp/$B
echo "=== tex state dump (textured binds) ===" | tee -a "$RESULT"
grep -a "texstate\[" /tmp/mach64gld.log | tail -16 | tee -a "$RESULT"
echo "--- done; output in texfilter-out.txt ---"
