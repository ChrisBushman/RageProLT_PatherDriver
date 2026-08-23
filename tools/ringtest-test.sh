#!/bin/sh
# End-to-end kext DMA-ring test: load kext, fill the command buffer + call
# new_vertex_buffer (kext drives the ring), read back, unload.
#   sudo sh ringtest-test.sh
set -e
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
echo "--- staging + loading kext ---"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
echo "--- mach64_ringtest (kext-driven DMA gouraud) ---"
"$DIR/mach64_ringtest" || echo "(returned non-zero)"
echo "--- unloading ---"; kextunload /tmp/$B; echo done.
