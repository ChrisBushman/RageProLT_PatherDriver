#!/bin/sh
# Validate the mipmapped TEX_BLEND_FCN_LINEAR (trilinear) path -- now enabled by DEFAULT for mipmapped
# textures (nlevels>1), so NO probe arg is needed; this exercises the shipping default.  Installs the
# bundle+kext, runs mach64_texmip (full mip chain, distinct colour per level, drawn minified).
# PASS = neither quad BLACK and no hang (mip-blend fetched real levels); trilinear = LEFT green > RIGHT.
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
rm -rf "$ROOT/gldriver/ATIRageProGLDriver.bundle"; rm -f "$DIR/mach64_texmip"
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF" bundle ) || { echo "BUNDLE BUILD FAILED"; exit 1; }
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$ROOT/gldriver/ATIRageProGLDriver.bundle" "$DST"; chown -R root:wheel "$DST"
kextunload /tmp/$B 2>/dev/null
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_texmip" "$DIR/mach64_texmip.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 | grep -viE "warning:" ; true
[ -x "$DIR/mach64_texmip" ] || { echo "TEST BUILD FAILED"; kextunload /tmp/$B; exit 1; }
RESULT="$ROOT/texmip-out.txt"
# No arg: pure mipmap-emit-fix test -- both quads use nearest mip (trilinear bit is probe-gated off), so
# PASS = both render the level colour (RED at this LOD), not BLACK.  `probe` also arms trilinear on the
# LEFT quad (LINEAR_MIPMAP_LINEAR) so LEFT then shows the L0+L1 blend (green>0) vs RIGHT pure red.
if [ "$1" = "probe" ]; then
    echo "=== ARMED: GLD_TEXBLEND_PROBE=1 (trilinear bit ON for LEFT) ===" | tee "$RESULT"
    GLD_TEXBLEND_PROBE=1 "$DIR/mach64_texmip" 2>&1 | tee -a "$RESULT"
else
    echo "=== mipmap-fix test: bit OFF, both quads should render RED not BLACK ===" | tee "$RESULT"
    "$DIR/mach64_texmip" 2>&1 | tee -a "$RESULT"
fi
kextunload /tmp/$B
echo "=== mach64 tex: log ===" | tee -a "$RESULT"
grep -aE "mach64 texture upload:|mach64 tex:" /tmp/mach64gld.log | tail -8 | tee -a "$RESULT"
echo "--- done; output in texmip-out.txt ---"
