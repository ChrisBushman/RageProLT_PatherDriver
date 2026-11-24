#!/bin/sh
# Dump the card's expansion ROM: load Mach64Accelerator, run mach64_romdump, unload.  Run as root.
set -e
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B
kextload /tmp/$B
"$DIR/mach64_romdump" "$@" || echo "(romdump returned non-zero)"
kextunload /tmp/$B
