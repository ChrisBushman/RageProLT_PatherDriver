#!/bin/sh
# STEP 1 of the Apple-present wire-up: put KNOWN pixels into the surface via surface_write_lock and
# confirm Apple's surface_flush presents THEM (the (0,0) square turns solid RED instead of garbage).
# DUMP-first: prints the IOAccelSurfaceData (address/pitch) to learn the memory model, then writes
# red only if the returned address is a plausible userspace pointer.  Talks only to Apple's loaded
# kext; does NOT load our kext/bundle.  Panic-prone (write_lock maps/locks the surface) -- run ssh:
#   sudo sh tools/writetest.sh            # idx 0x13 by default
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`
cc -O2 -o "$DIR/mach64_writetest" "$DIR/mach64_writetest.c" -framework IOKit 2>&1 \
    || { echo "BUILD FAILED"; exit 1; }
echo "=== running writetest (idx=${1:-0x13}) ==="
"$DIR/mach64_writetest" ${1:+"$1"} 2>&1 | tee "$ROOT/writetest-out.txt"
echo "--- done; output in writetest-out.txt ---"
