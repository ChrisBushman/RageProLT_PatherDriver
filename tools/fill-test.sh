#!/bin/sh
# Off-screen solid-fill engine test: load kext, run mach64_fill, unload.
# Fills 6MB into VRAM (off-screen), saves/restores engine context, verifies by
# reading VRAM back.  First side-effecting engine use -- see mach64_fill.c.
#
# Build first:  make -C kext kext && make -C tools
# Run as root:  sudo sh tools/fill-test.sh
set -e
DIR=`cd \`dirname "$0"\` && pwd`
ROOT=`cd "$DIR/.." && pwd`
B=Mach64Accelerator.kext

echo "--- staging + loading kext ---"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B
kextload /tmp/$B

echo "--- mach64_fill (off-screen 2D engine fill + readback) ---"
"$DIR/mach64_fill" || echo "(fill returned non-zero)"

echo "--- unloading ---"
kextunload /tmp/$B
echo "done."
