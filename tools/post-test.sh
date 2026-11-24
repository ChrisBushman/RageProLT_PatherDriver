#!/bin/sh
# Replay the BIOS bring-up on the headless Rage and re-test VRAM.  usage: sudo sh post-test.sh [full]
set -e
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B
kextload /tmp/$B
"$DIR/mach64_post" "$@" || echo "(post returned non-zero)"
kextunload /tmp/$B
