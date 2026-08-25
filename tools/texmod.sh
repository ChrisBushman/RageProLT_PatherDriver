#!/bin/sh
# GL_MODULATE with NON-WHITE vertex colour -> confirm the texture is multiplied
# by the vertex colour, not replaced.  sudo sh texmod.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_texmod" "$DIR/mach64_texmod.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_texmod"
cp -f /tmp/mach64gld.log "$ROOT/texmod-log.txt" 2>/dev/null
echo "=== textured draws (SCALE_3D_CNTL should carry TEX_LIGHT_FCN_MODULATE) ==="
grep -aE "texture upload|render_triangles" /tmp/mach64gld.log 2>/dev/null | head -20
kextunload /tmp/$B; echo "--- done ---"
