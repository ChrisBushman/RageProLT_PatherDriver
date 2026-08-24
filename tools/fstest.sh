#!/bin/sh
# Install bundle + kext, run the windowed AGL app to trigger gldAttachDrawable and
# capture the CGS drawable descriptor.  This build does NOT render (safe probe).
# NOTE: a Carbon window needs the WindowServer session -- if run over ssh it may
# fail to make the window; if so, run it from Terminal.app on the Lombard console.
#   sudo sh agltest.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
echo "--- installing bundle + kext ---"
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
echo "--- running FULL-SCREEN AGL app (attach only, no render) ---"
"$DIR/mach64_fstest"
echo "--- /tmp/mach64gld.log ---"; cat /tmp/mach64gld.log 2>/dev/null
echo "--- unloading kext ---"; kextunload /tmp/$B
echo "--- done ---"
