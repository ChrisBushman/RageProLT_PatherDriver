#!/bin/sh
# usage: sudo sh run-live.sh [biosrun args]   (kext must be built in ../../kext; ROM at ../rage.rom, never committed; UDP host below)
B=Mach64Accelerator.kext; D=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$D/../.." && pwd`
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B
kextload /tmp/$B
"$D/biosrun" -H -u 192.168.5.213 -S -t /Users/sam/biosrun_live.txt "$@" "$D/../rage.rom"
echo "biosrun exit: $?"
kextunload /tmp/$B
