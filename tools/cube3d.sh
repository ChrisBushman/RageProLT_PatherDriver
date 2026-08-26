#!/bin/sh
# First textured 3D scene: a perspective, depth-buffered, checkerboard-textured cube.
# Default build.  PASS = a SOLID cube (3 faces visible), checker squares perspective-
# foreshortened toward the far edges, no back faces bleeding through, clean face edges.
#   uniform (non-foreshortened) checker  => perspective-correct texturing broken (W)
#   see-through / overlapping faces       => depth test not occluding
# Run on the Lombard under sudo:  sudo sh tools/cube3d.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; make bundle ) || exit 1
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_cube3d" "$DIR/mach64_cube3d.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_cube3d"
kextunload /tmp/$B
cp -f /tmp/mach64gld.log "$ROOT/cube3d-log.txt" 2>/dev/null
echo "=== screenshot: solid checker cube, 3 faces, perspective-foreshortened ==="
echo "--- done (log in cube3d-log.txt) ---"
