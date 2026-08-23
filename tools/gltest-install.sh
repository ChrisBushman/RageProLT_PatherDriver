#!/bin/sh
# Install our GL driver into the slot Apple's ATIRagePro.kext already points at
# (IOGLBundleName=ATIRageProGLDriver), load the kext, run an accelerated CGL app,
# and show the framework's trace of how it drove our bundle.
#   sudo sh gltest-install.sh
set -e
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle

if [ ! -d "$GLB" ]; then echo "build the bundle first: make -C gldriver bundle"; exit 1; fi
echo "--- installing GL driver as $DST (the name ATIRagePro.kext already loads) ---"
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
echo "--- loading Mach64Accelerator kext (our bundle opens it) ---"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
echo "--- running accelerated CGL app (triggers renderer discovery) ---"
rm -f /tmp/mach64gld.log
"$DIR/mach64_gltest" || echo "(gltest returned non-zero)"
echo
echo "=== /tmp/mach64gld.log (framework -> our bundle trace) ==="
cat /tmp/mach64gld.log 2>/dev/null || echo "(EMPTY: framework did NOT load our bundle)"
echo "--- unloading kext (leaving the GL bundle installed for iteration) ---"
kextunload /tmp/$B || true
echo "(remove the GL bundle when done:  sudo rm -rf $DST )"
echo done.
