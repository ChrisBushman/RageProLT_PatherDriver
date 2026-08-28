#!/bin/sh
# Does GL_LUMINANCE work / does GLEngine expand it?  Installs the bundle+kext, runs mach64_texfmt
# (2x2 luminance texture, reads each texel).  The "mach64 tex:" log line shows the fmt GLEngine gave
# (0x1908 = expanded to RGBA -> already works; 0x1909 = raw luminance -> driver must convert).
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
rm -rf "$ROOT/gldriver/ATIRageProGLDriver.bundle"; rm -f "$DIR/mach64_texfmt"
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF" bundle ) || { echo "BUNDLE BUILD FAILED"; exit 1; }
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$ROOT/gldriver/ATIRageProGLDriver.bundle" "$DST"; chown -R root:wheel "$DST"
kextunload /tmp/$B 2>/dev/null
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_texfmt" "$DIR/mach64_texfmt.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 | grep -viE "warning:" ; true
[ -x "$DIR/mach64_texfmt" ] || { echo "TEST BUILD FAILED"; kextunload /tmp/$B; exit 1; }
RESULT="$ROOT/texfmt-out.txt"
"$DIR/mach64_texfmt" 2>&1 | tee "$RESULT"
kextunload /tmp/$B
echo "=== mach64 tex: log (fmt GLEngine handed us) ===" | tee -a "$RESULT"
grep -aE "mach64 texture upload:|texel src" /tmp/mach64gld.log | tail -4 | tee -a "$RESULT"
echo "--- done; output in texfmt-out.txt ---"
