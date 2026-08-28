#!/bin/sh
# Probe glCopyTexImage2D: does it work, and how does GLEngine dispatch it?  Installs bundle+kext,
# runs mach64_copyteximage, then shows the driver log so we can see whether GLEngine decomposed the
# copy into a texture upload / a readback, or dispatched a dedicated hook we don't handle.
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
rm -rf "$ROOT/gldriver/ATIRageProGLDriver.bundle"; rm -f "$DIR/mach64_copyteximage"
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF" bundle ) || { echo "BUNDLE BUILD FAILED"; exit 1; }
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$ROOT/gldriver/ATIRageProGLDriver.bundle" "$DST"; chown -R root:wheel "$DST"
kextunload /tmp/$B 2>/dev/null
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_copyteximage" "$DIR/mach64_copyteximage.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 | grep -viE "warning:" ; true
[ -x "$DIR/mach64_copyteximage" ] || { echo "TEST BUILD FAILED"; kextunload /tmp/$B; exit 1; }
RESULT="$ROOT/copyteximage-out.txt"
"$DIR/mach64_copyteximage" 2>&1 | tee "$RESULT"
kextunload /tmp/$B
echo "=== driver log: texture upload / readback / dispatch around the copy ===" | tee -a "$RESULT"
grep -aE "texture upload:|READBACK|ctl0|dispatch|CopyTex|render_ctl" /tmp/mach64gld.log 2>/dev/null | tail -20 | tee -a "$RESULT"
cp -f /tmp/mach64gld.log "$ROOT/copyteximage-log.txt" 2>/dev/null
echo "--- done; output in copyteximage-out.txt ---"
