#!/bin/sh
# Verify glDrawElements (indexed) rendering.  Requires the NORMAL driver build
# (no -DGLD_DISPATCH_PROBE).  Four indexed draws of the four-quadrant texture ->
# four identical clean squares.  sudo sh texelem.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_texelem" "$DIR/mach64_texelem.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_texelem"
kextunload /tmp/$B; echo "--- done ---"
