#!/bin/sh
# Large textures via chunked host-data blits: 64x64 + 128x128 gradients.
# Smooth gradient = chunking correct; horizontal seams = band bug.  sudo sh texbig.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_texbig" "$DIR/mach64_texbig.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_texbig"
cp -f /tmp/mach64gld.log "$ROOT/texbig-log.txt" 2>/dev/null
echo "=== uploads (expect 64x64 4096 texels, 128x128 16384 texels) ==="
grep -aE "texture upload" /tmp/mach64gld.log 2>/dev/null | head
kextunload /tmp/$B; echo "--- done ---"
