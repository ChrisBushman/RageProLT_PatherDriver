#!/bin/sh
# 3D triangle test: load kext, run mach64_tri, unload.
# Rasterizes a solid triangle into off-screen VRAM (6MB in), saves/restores
# engine context, verifies by readback.  First 3D setup-engine use.
#
# Build first:  make -C kext kext && make -C tools
# Run as root:  sudo sh tools/tri-test.sh
set -e
DIR=`cd \`dirname "$0"\` && pwd`
ROOT=`cd "$DIR/.." && pwd`
B=Mach64Accelerator.kext

echo "--- staging + loading kext ---"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B
kextload /tmp/$B

echo "--- mach64_tri (off-screen 3D triangle + readback) ---"
"$DIR/mach64_tri" || echo "(tri returned non-zero)"

echo "--- unloading ---"
kextunload /tmp/$B
echo "done."
