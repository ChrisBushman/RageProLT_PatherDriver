#!/bin/sh
# Decode gldCreateTextureLevel's a6 arg: upload known non-square textures and
# read the a6 our gld_log prints for each (correlate by ORDER with the app's
# probe[] list).   sudo sh texprobe.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_texprobe" "$DIR/mach64_texprobe.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_texprobe"
cp -f /tmp/mach64gld.log "$ROOT/texprobe-log.txt" 2>/dev/null
echo "=== gldCreateTextureLevel a6 values (in probe order) ==="
grep -a "gldCreateTextureLevel" /tmp/mach64gld.log 2>/dev/null
kextunload /tmp/$B; echo "--- done (full log in ~/RageProLT/texprobe-log.txt) ---"
