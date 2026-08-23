#!/bin/sh
# Minimal bus-master DMA test: load kext, run mach64_dmatest, unload.
set -e
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
echo "--- staging + loading kext ---"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
echo "--- mach64_dmatest (DMA one register write) ---"
"$DIR/mach64_dmatest" || echo "(returned non-zero)"
echo "--- unloading ---"; kextunload /tmp/$B; echo done.
