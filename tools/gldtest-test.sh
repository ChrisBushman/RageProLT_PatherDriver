#!/bin/sh
# Prove the gld translation core on hardware: load kext, run mach64_gldtest
# (drives the REAL gld_render/gld_state/mach64_emit code), read back, unload.
#   sudo sh gldtest-test.sh
set -e
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
echo "--- staging + loading kext ---"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
echo "--- mach64_gldtest (gld translation core -> kext ring) ---"
"$DIR/mach64_gldtest" || echo "(returned non-zero)"
echo "--- unloading ---"; kextunload /tmp/$B; echo done.
