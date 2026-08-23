#!/bin/sh
# Install bundle + kext, run the accelerated CGL app, report npix + our gld log.
# The win condition: CGLChoosePixelFormat -> npix > 0 (framework accepted a
# config our gldChoosePixelFormat offered).
#   sudo sh pftest.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
echo "--- installing bundle + kext ---"
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
echo "--- running accelerated CGL app ---"
"$DIR/mach64_gltest"
echo "--- /tmp/mach64gld.log ---"; cat /tmp/mach64gld.log 2>/dev/null
echo "--- unloading kext ---"; kextunload /tmp/$B
echo "--- done (bundle left installed; rm it when finished testing) ---"
