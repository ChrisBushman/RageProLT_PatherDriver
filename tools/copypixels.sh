#!/bin/sh
# Verify glCopyPixels(GL_COLOR).  Rebuilds the GL bundle (mach64_ctl10 = copy blit) + kext, installs,
# runs mach64_copypixels (render white quad -> glCopyPixels -> grid-scan for the white squares).
# Run on the Lombard under sudo (console session; needs our accelerated renderer).
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext

# Force-remove stale (possibly root-owned) build artifacts so a failed build can't silently run an
# old binary.  This script runs under sudo, so it can clear them.
rm -rf "$ROOT/gldriver/ATIRageProGLDriver.bundle"
rm -f  "$DIR/mach64_copypixels"

( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF" bundle ) || { echo "BUNDLE BUILD FAILED"; exit 1; }
[ -d "$ROOT/gldriver/ATIRageProGLDriver.bundle" ] || { echo "BUNDLE NOT BUILT"; exit 1; }
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$ROOT/gldriver/ATIRageProGLDriver.bundle" "$DST"; chown -R root:wheel "$DST"

kextunload /tmp/$B 2>/dev/null
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log

cc -o "$DIR/mach64_copypixels" "$DIR/mach64_copypixels.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 | grep -viE "warning:" ; true
[ -x "$DIR/mach64_copypixels" ] || { echo "TEST BUILD FAILED"; kextunload /tmp/$B; exit 1; }
echo "test binary: `ls -l "$DIR/mach64_copypixels" | awk '{print $5, $6, $7, $8}'`"

RESULT="$ROOT/copypixels-out.txt"
"$DIR/mach64_copypixels" 2>&1 | tee "$RESULT"
kextunload /tmp/$B
echo "=== glCopyPixels log ===" | tee -a "$RESULT"
grep -a "glCopyPixels" /tmp/mach64gld.log | tail -4 | tee -a "$RESULT"
echo "--- done; output in copypixels-out.txt ---"
