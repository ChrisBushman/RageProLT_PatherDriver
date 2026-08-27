#!/bin/sh
# Verify GL_CULL_FACE.  Default build.  Runs mach64_culltest: frame 1 culling OFF (a GREEN
# front-facing + a RED back-facing triangle both show), frame 2 glCullFace(GL_BACK) + cull ON.
# PASS = frame 2 shows ONLY the green (front) triangle; the red (back) one is gone.  GLEngine
# culls host-side (it dispatches n=3 not n=6 -- verified in the log), so the driver needs no
# cull code; this just confirms the surviving front face renders.  Run:  sudo sh tools/culltest.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; make bundle ) || exit 1
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_culltest" "$DIR/mach64_culltest.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_culltest"
kextunload /tmp/$B
echo "=== render dispatch (frame 2 should be n=3 -- GLEngine culled the back triangle) ==="
grep -nE "render_triangles" /tmp/mach64gld.log | tail -4
cp -f /tmp/mach64gld.log "$ROOT/culltest-log.txt" 2>/dev/null
echo "--- screenshot frame 2: only the GREEN (front) triangle, red back one gone ---"
