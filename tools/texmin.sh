#!/bin/sh
# Is MINIFICATION filtered on the current driver?  Installs the current bundle+kext and runs
# mach64_texmin: a 2x2 checkerboard drawn heavily minified, LEFT=GL_LINEAR RIGHT=GL_NEAREST.
# LEFT avg~128 tight spread = BILINEAR already covers minification (TEX_BLEND_FCN not needed);
# LEFT 0/255 wide spread like RIGHT = minification is nearest -> TEX_BLEND_FCN needed.
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
rm -rf "$ROOT/gldriver/ATIRageProGLDriver.bundle"; rm -f "$DIR/mach64_texmin"
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF" bundle ) || { echo "BUNDLE BUILD FAILED"; exit 1; }
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$ROOT/gldriver/ATIRageProGLDriver.bundle" "$DST"; chown -R root:wheel "$DST"
kextunload /tmp/$B 2>/dev/null
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_texmin" "$DIR/mach64_texmin.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 | grep -viE "warning:" ; true
[ -x "$DIR/mach64_texmin" ] || { echo "TEST BUILD FAILED"; kextunload /tmp/$B; exit 1; }
RESULT="$ROOT/texmin-out.txt"
# Safe by default: the TEX_BLEND_FCN bit stays OFF unless you pass "probe".  A plain run confirms the
# 128x128 texture upload + minification + readback path is itself stable (isolating it from the bit);
# `texmin.sh probe` arms the unverified bit for the actual filter test.
if [ "$1" = "probe" ]; then
    echo "=== ARMED: GLD_TEXBLEND_PROBE=1 (TEX_BLEND_FCN_LINEAR bit ON) ===" | tee "$RESULT"
    GLD_TEXBLEND_PROBE=1 "$DIR/mach64_texmin" 2>&1 | tee -a "$RESULT"
else
    echo "=== SAFE: bit OFF (pass 'probe' to arm TEX_BLEND_FCN) ===" | tee "$RESULT"
    "$DIR/mach64_texmin" 2>&1 | tee -a "$RESULT"
fi
kextunload /tmp/$B
echo "=== mach64 tex: log ===" | tee -a "$RESULT"
grep -aE "mach64 texture upload:|scale_3d|SCALE_3D" /tmp/mach64gld.log | tail -6 | tee -a "$RESULT"
echo "--- done; output in texmin-out.txt ---"
