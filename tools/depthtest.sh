#!/bin/sh
# Standalone hardware Z-buffer proof (off-screen readback, no display/GLEngine).
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B
kextload /tmp/$B 2>/dev/null
(cd "$DIR" && make mach64_depthtest 2>&1 | grep -iE "error" )
"$DIR/mach64_depthtest"
kextunload /tmp/$B 2>/dev/null
