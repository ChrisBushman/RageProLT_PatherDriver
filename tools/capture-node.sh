#!/bin/sh
# Capture the framework's pixel-format config-node LIST at runtime (accelerated
# path), so we see valid nodes next to ours.  Installs our bundle + kext, runs
# the gdb capture, cleans up the kext (leaves the bundle for iteration).
#   sudo sh capture-node.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
echo "--- installing bundle + kext ---"
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
echo "--- gdb capture: pf node list (accelerated) ---"
gdb -batch -x "$DIR/capture-node.gdb" "$DIR/mach64_gltest" 2>&1
echo "--- unloading kext ---"; kextunload /tmp/$B
echo "--- done (bundle left installed; rm it when finished testing) ---"
