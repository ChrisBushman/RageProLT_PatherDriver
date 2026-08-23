#!/bin/sh
# FIRST LIGHT: load the kext, CFBundle-load ATIRageProGLDriver.bundle, drive the full
# gld lifecycle to render a triangle, read back, unload.
#   sudo sh firstlight-test.sh
set -e
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
BUNDLE="$ROOT/gldriver/ATIRageProGLDriver.bundle"
if [ ! -d "$BUNDLE" ]; then echo "bundle missing: $BUNDLE (run: make -C gldriver bundle)"; exit 1; fi
echo "--- staging + loading kext ---"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
echo "--- mach64_firstlight $1 (CFBundle-load the GL driver) ---"
"$DIR/mach64_firstlight" "$BUNDLE" "$1" || echo "(returned non-zero)"
echo "--- unloading ---"; kextunload /tmp/$B; echo done.
