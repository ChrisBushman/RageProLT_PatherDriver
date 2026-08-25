#!/bin/sh
# Framebuffer alpha blending: RGBA texel alpha blends the quad over a blue bg.
# Q0 blend -> blue->red ramp; Q1 no-blend -> solid red.  sudo sh texblend.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_texblend" "$DIR/mach64_texblend.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_texblend"
cp -f /tmp/mach64gld.log "$ROOT/texblend-log.txt" 2>/dev/null
echo "=== bind blend field (expect Q0 blend=0x2c0000 srcalpha|invsrcalpha, Q1 blend=0x10000) ==="
grep -aE "mach64 tex: env=" /tmp/mach64gld.log 2>/dev/null | head
kextunload /tmp/$B; echo "--- done ---"
