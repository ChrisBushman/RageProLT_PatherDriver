#!/bin/sh
# Gouraud test via BM_ADDR packet path: load kext, run mach64_gouraud, unload.
set -e
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
echo "--- staging + loading kext ---"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
echo "--- mach64_gouraud (vertices via BM_ADDR, per-vertex color) ---"
"$DIR/mach64_gouraud" || echo "(returned non-zero)"
echo "--- unloading ---"; kextunload /tmp/$B; echo done.
