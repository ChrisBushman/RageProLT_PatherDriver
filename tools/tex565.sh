#!/bin/sh
# Does GL_UNSIGNED_SHORT_5_6_5 work?  Installs bundle+kext, runs mach64_tex565 (2x2 RGB565 texture).
# PASS = red/green/blue/white read pure; the "mach64 texture upload:" fmt= line shows what GLEngine
# handed us (pre-expanded 8-bit -> works; raw 16-bit -> our converter misreads).
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
rm -rf "$ROOT/gldriver/ATIRageProGLDriver.bundle"; rm -f "$DIR/mach64_tex565"
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF -DGLD_TEXFMT_PROBE" bundle ) || { echo "BUNDLE BUILD FAILED"; exit 1; }
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$ROOT/gldriver/ATIRageProGLDriver.bundle" "$DST"; chown -R root:wheel "$DST"
kextunload /tmp/$B 2>/dev/null
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_tex565" "$DIR/mach64_tex565.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 | grep -viE "warning:" ; true
[ -x "$DIR/mach64_tex565" ] || { echo "TEST BUILD FAILED"; kextunload /tmp/$B; exit 1; }
RESULT="$ROOT/tex565-out.txt"
"$DIR/mach64_tex565" 2>&1 | tee "$RESULT"
kextunload /tmp/$B
echo "=== mach64 tex: log (upload fmt + level records to compare) ===" | tee -a "$RESULT"
grep -aE "mach64 texture upload:|texrec L0:" /tmp/mach64gld.log | tail -6 | tee -a "$RESULT"
echo "--- done; output in tex565-out.txt ---"
