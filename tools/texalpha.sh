#!/bin/sh
# RGBA texel alpha via GL_DECAL (TEX_MAP_AEN): Q0 blue->red ramp, Q1 solid red.
# sudo sh texalpha.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_texalpha" "$DIR/mach64_texalpha.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_texalpha"
cp -f /tmp/mach64gld.log "$ROOT/texalpha-log.txt" 2>/dev/null
echo "=== upload fmt + bind alpha flag (expect fmt=0x1908, alpha=1) ==="
grep -aE "texture upload|mach64 tex: env=" /tmp/mach64gld.log 2>/dev/null | head
kextunload /tmp/$B; echo "--- done ---"
