#!/bin/sh
# Two distinct textures bound + drawn separately -> validate the texture VRAM
# allocator and reveal how binding is signalled.  sudo sh texmulti.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_texmulti" "$DIR/mach64_texmulti.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_texmulti"
cp -f /tmp/mach64gld.log "$ROOT/texmulti-log.txt" 2>/dev/null
echo "=== gld texture-call sequence ==="
grep -aE "gldModifyTexture|texture upload|gldCreateTexture\(" /tmp/mach64gld.log 2>/dev/null | head -30
kextunload /tmp/$B; echo "--- done ---"
