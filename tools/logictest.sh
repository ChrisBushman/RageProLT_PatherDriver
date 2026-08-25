#!/bin/sh
# HW-verify GL_COLOR_LOGIC_OP.  Rebuilds the renderer bundle (default flags), installs
# it, and runs mach64_logictest.  Run on the Lombard under sudo.
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; make bundle ) || exit 1
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_logictest" "$DIR/mach64_logictest.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_logictest"
kextunload /tmp/$B; echo "--- done ---"
