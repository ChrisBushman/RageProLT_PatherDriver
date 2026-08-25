#!/bin/sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
# Rebuild the renderer bundle with DEFAULT flags (the last build may have been the
# -DGLD_FOGSCAN instrumented one); this exercises the activated GL_FOG path.
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; make bundle ) || exit 1
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_fogtest" "$DIR/mach64_fogtest.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_fogtest"
kextunload /tmp/$B; echo "--- done ---"
