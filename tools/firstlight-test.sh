#!/bin/sh
# FIRST LIGHT: load the kext, CFBundle-load Mach64GLDriver.bundle, drive the full
# gld lifecycle to render a triangle, read back, unload.
#   sudo sh firstlight-test.sh
set -e
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
BUNDLE="$ROOT/gldriver/Mach64GLDriver.bundle"
if [ ! -d "$BUNDLE" ]; then echo "bundle missing: $BUNDLE (run: make -C gldriver bundle)"; exit 1; fi
echo "--- staging + loading kext ---"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
echo "--- mach64_firstlight (CFBundle-load the GL driver -> render) ---"
"$DIR/mach64_firstlight" "$BUNDLE" || echo "(returned non-zero)"
echo "--- unloading ---"; kextunload /tmp/$B; echo done.
