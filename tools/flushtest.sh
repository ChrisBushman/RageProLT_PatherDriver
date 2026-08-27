#!/bin/sh
# LIVE validation that Apple's PRESENT path (surface_flush -> build_swap -> submit_buffer) runs
# engine-INDEPENDENTLY -- confirming the static crack that Apple's "swap" is a PIO 2D blit that
# never touches the stubbed start_engine (docs/re-notes/apple-glcontext-protocol.md, 2026-08-27).
# Drives the VALIDATED surface handshake then fires surface_flush.  Talks DIRECTLY to Apple's
# already-loaded ATIRagePro.kext; does NOT load our kext/bundle.
#
# PANIC-PRONE (expected at this integration stage): surface_flush allocs surfaces + PIO-writes the
# engine.  Breadcrumbs (>>> lines) print+flush before each risky call, so a panic still shows how
# far we got.  Run from ssh so a hang is recoverable:
#   sudo sh tools/flushtest.sh            # binds idx 0x13 by default
#   sudo sh tools/flushtest.sh 0x1b       # or a specific idx
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`

cc -O2 -o "$DIR/mach64_flushtest" "$DIR/mach64_flushtest.c" -framework IOKit 2>&1 \
    || { echo "BUILD FAILED"; exit 1; }

echo "=== running flushtest (idx=${1:-0x13}) ==="
"$DIR/mach64_flushtest" ${1:+"$1"} 2>&1 | tee "$ROOT/flushtest-out.txt"
echo "--- done; output in flushtest-out.txt ---"
