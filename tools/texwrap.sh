#!/bin/sh
# Wrap-mode support: REPEAT tiles (3x3 grid of markers), CLAMP shows one marker.
# sudo sh texwrap.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_texwrap" "$DIR/mach64_texwrap.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_texwrap"
cp -f /tmp/mach64gld.log "$ROOT/texwrap-log.txt" 2>/dev/null
kextunload /tmp/$B; echo "--- done ---"
