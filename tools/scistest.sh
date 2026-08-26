#!/bin/sh
# Verify hardware GL_SCISSOR_TEST.  Default build (scissor implemented).  Runs
# mach64_scissortest: frame 1 draws a full white quad with scissor OFF, frame 2 re-draws
# it with glScissor(101,203,305,407) + GL_SCISSOR_TEST ON.  The on-screen result is frame 2.
#   PASS: white shows ONLY inside the box -- a 305x407 rectangle, LEFT edge 101 px from the
#         left, sitting NEARER THE TOP (top margin 158 < bottom margin 203).  That proves the
#         box AND the GL bottom-left -> FB top-left Y-flip.
#   FAIL(no flip): rectangle sits nearer the BOTTOM (top margin 203).  FAIL(ignored): quad full.
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; make bundle ) || exit 1
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_scissortest" "$DIR/mach64_scissortest.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_scissortest"
kextunload /tmp/$B
cp -f /tmp/mach64gld.log "$ROOT/scistest-log.txt" 2>/dev/null
echo "--- done (screenshot frame 2: white box, left-of-centre, nearer the TOP) ---"
