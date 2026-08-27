#!/bin/sh
# STEP 2a: confirm Apple's surface_flush present covers the FULL SCREEN.  Shapes a full-screen
# surface, paints R|G|B|White bands + white border, flushes.  Standalone (Apple kext only; no our
# kext).  Panic-prone.  Paints the whole screen (clears on next desktop repaint).  Run from ssh:
#   sudo sh tools/fspresent.sh           # 1024x768 default
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`
cc -O2 -o "$DIR/mach64_fspresent" "$DIR/mach64_fspresent.c" -framework IOKit 2>&1 \
    || { echo "BUILD FAILED"; exit 1; }
echo "=== running fspresent ==="
"$DIR/mach64_fspresent" 2>&1 | tee "$ROOT/fspresent-out.txt"
echo "--- done; output in fspresent-out.txt ---"
