#!/bin/sh
# Visually confirm GL_TEXTURE_ENV_MODE is honored: orange texture * cyan colour ->
# MODULATE green, REPLACE/DECAL orange.  sudo sh texenvshow.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_texenvshow" "$DIR/mach64_texenvshow.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_texenvshow"
cp -f /tmp/mach64gld.log "$ROOT/texenvshow-log.txt" 2>/dev/null
echo "=== per-mode light_fcn (expect 1=MODULATE, 0=REPLACE, 2=DECAL) ==="
grep -aE "mach64 tex: env=" /tmp/mach64gld.log 2>/dev/null | head
kextunload /tmp/$B; echo "--- done ---"
