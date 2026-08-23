#!/bin/sh
# Prove the render-slot bodies + gldUpdateDispatch on hardware: load kext, run
# mach64_primtest (drives a synthetic vertex array THROUGH the dispatch table),
# read back, unload.
#   sudo sh primtest-test.sh
set -e
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
echo "--- staging + loading kext ---"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
echo "--- mach64_primtest (dispatch table -> render body -> kext ring) ---"
"$DIR/mach64_primtest" || echo "(returned non-zero)"
echo "--- unloading ---"; kextunload /tmp/$B; echo done.
