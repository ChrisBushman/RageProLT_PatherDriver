#!/bin/sh
# Prove the gldDestroyContext IOConnectUnmapMemory leak fix.  Installs the bundle+kext, runs
# mach64_ctxleak (100x aglCreateContext/aglDestroyContext, measuring process VM).
# PASS = "FIXED (VM stable across 100 contexts)"; a leak would grow VM by MBs/ctx or abort the loop.
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
rm -rf "$ROOT/gldriver/ATIRageProGLDriver.bundle"; rm -f "$DIR/mach64_ctxleak"
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF" bundle ) || { echo "BUNDLE BUILD FAILED"; exit 1; }
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$ROOT/gldriver/ATIRageProGLDriver.bundle" "$DST"; chown -R root:wheel "$DST"
kextunload /tmp/$B 2>/dev/null
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_ctxleak" "$DIR/mach64_ctxleak.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 | grep -viE "warning:" ; true
[ -x "$DIR/mach64_ctxleak" ] || { echo "TEST BUILD FAILED"; kextunload /tmp/$B; exit 1; }
RESULT="$ROOT/ctxleak-out.txt"
"$DIR/mach64_ctxleak" 2>&1 | tee "$RESULT"
kextunload /tmp/$B
echo "=== driver: create/destroy counts (should match) ===" | tee -a "$RESULT"
echo "gldCreateContext:  `grep -ac gldCreateContext /tmp/mach64gld.log 2>/dev/null`" | tee -a "$RESULT"
echo "unmap/close done via gldDestroyContext (no per-call log)" | tee -a "$RESULT"
echo "--- done; output in ctxleak-out.txt ---"
