#!/bin/sh
# Test hardware GL_POLYGON_STIPPLE.  Default build (stipple implemented).  Draws two big
# white quads on blue: LEFT stipple OFF (solid), RIGHT stipple ON with a coarse stripe
# pattern.  PASS = RIGHT quad shows the stripes (8x8 hardware pattern gates the 3D
# scaler); FAIL = RIGHT solid (pattern ignored) or garbage.
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; make bundle ) || exit 1
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_stiptest" "$DIR/mach64_stiptest.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_stiptest"
kextunload /tmp/$B
echo "=== render dispatch (did the stipple quad reach our slots?) + state delta ==="
grep -anE "render_quads|render_triangles|render_polygon|FOGSCAN delta|gli\+0x" /tmp/mach64gld.log | head -30
cp -f /tmp/mach64gld.log "$ROOT/stiptest-log.txt" 2>/dev/null
echo "--- done ---"
