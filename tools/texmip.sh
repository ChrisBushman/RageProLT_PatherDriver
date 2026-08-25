#!/bin/sh
# Mipmap probe/test: full mip chain (distinct colour per level) drawn minified.
# The "mip Ln:" log lines show whether GLEngine provides the level records.
# sudo sh texmip.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_texmip" "$DIR/mach64_texmip.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_texmip"
cp -f /tmp/mach64gld.log "$ROOT/texmip-log.txt" 2>/dev/null
echo "=== mip level records provided by GLEngine ==="
grep -aE "texture upload|mip L" /tmp/mach64gld.log 2>/dev/null | head -20
kextunload /tmp/$B; echo "--- done ---"
