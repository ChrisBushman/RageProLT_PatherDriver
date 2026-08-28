#!/bin/sh
# Multi-window disambiguation PROBE.  Installs bundle+kext, runs mach64_multiwin (two GL windows).
# Look at the two descdump lines (does any field beyond d[0] distinguish the windows + correlate with
# their CGS bounds?) and the two [cgs]/[win] lines (both resolving to the SAME rect = the heuristic bug).
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
rm -rf "$ROOT/gldriver/ATIRageProGLDriver.bundle"; rm -f "$DIR/mach64_multiwin"
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF" bundle ) || { echo "BUNDLE BUILD FAILED"; exit 1; }
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$ROOT/gldriver/ATIRageProGLDriver.bundle" "$DST"; chown -R root:wheel "$DST"
kextunload /tmp/$B 2>/dev/null
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_multiwin" "$DIR/mach64_multiwin.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 | grep -viE "warning:" ; true
[ -x "$DIR/mach64_multiwin" ] || { echo "TEST BUILD FAILED"; kextunload /tmp/$B; exit 1; }
RESULT="$ROOT/multiwin-out.txt"
"$DIR/mach64_multiwin" 2>&1 | tee "$RESULT"
kextunload /tmp/$B
echo "=== driver log: attach descriptors + CGS resolutions (per window) ===" | tee -a "$RESULT"
grep -aE "gldAttachDrawable|descdump|\[cgs\]|\[win\]" /tmp/mach64gld.log 2>/dev/null | tail -30 | tee -a "$RESULT"
echo "--- done; output in multiwin-out.txt ---"
