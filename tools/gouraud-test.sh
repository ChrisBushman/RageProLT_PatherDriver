#!/bin/sh
# Gouraud test via the bus-master DMA ring: load kext, run mach64_gouraud go, unload.
set -e
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
echo "--- staging + loading kext ---"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
echo "--- mach64_gouraud go (vertices via DMA ring, per-vertex color) ---"
"$DIR/mach64_gouraud" go || echo "(returned non-zero)"
echo "--- unloading ---"; kextunload /tmp/$B; echo done.
