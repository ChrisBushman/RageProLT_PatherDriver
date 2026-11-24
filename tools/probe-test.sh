#!/bin/sh
# Headless bring-up probe: load Mach64Accelerator, run mach64_probe, unload.  Run as root.
set -e
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B
kextload /tmp/$B
"$DIR/mach64_probe" || echo "(probe returned non-zero)"
kextunload /tmp/$B
