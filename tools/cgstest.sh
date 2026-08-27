#!/bin/sh
# Validate the CGS surface-ID -> window-bounds resolver (windowed handshake, option b).  Pure
# CoreGraphics + AGL, NO kext -> safe (no panic/lockup risk).  Must run in the console GUI session
# (Carbon needs the WindowServer):  sh tools/cgstest.sh   (sudo not required)
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`
cc -O2 -o "$DIR/mach64_cgstest" "$DIR/mach64_cgstest.c" \
    -framework Carbon -framework AGL -framework OpenGL -framework ApplicationServices 2>&1 \
    | grep -viE "warning:" ; true
[ -x "$DIR/mach64_cgstest" ] || { echo "BUILD FAILED"; exit 1; }
echo "=== running cgstest (window at 120,80 400x300) ==="
"$DIR/mach64_cgstest" 2>&1 | tee "$ROOT/cgstest-out.txt"
echo "--- done; output in cgstest-out.txt ---"
