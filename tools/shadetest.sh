#!/bin/sh
# Verify glShadeModel(GL_FLAT).  Default build.  Runs mach64_shadetest: frame 1 GL_SMOOTH
# (a red/green/blue gradient triangle), frame 2 GL_FLAT.  PASS = frame 2 is a SOLID BLUE
# triangle (the provoking = last vertex is blue); FAIL = frame 2 still shows the gradient.
# Run on the Lombard under sudo:  sudo sh tools/shadetest.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; make bundle ) || exit 1
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_shadetest" "$DIR/mach64_shadetest.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_shadetest"
kextunload /tmp/$B
cp -f /tmp/mach64gld.log "$ROOT/shadetest-log.txt" 2>/dev/null
echo "=== screenshot frame 2: a SOLID BLUE triangle = flat shading works ==="
echo "--- done ---"
