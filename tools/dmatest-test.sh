#!/bin/sh
# Bus-master DMA test.  Default = DRY RUN (validate bus addr, no DMA).
# Pass 'go' to actually activate DMA:  sudo sh dmatest-test.sh go
set -e
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
echo "--- staging + loading kext ---"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
echo "--- mach64_dmatest $1 ---"
"$DIR/mach64_dmatest" "$1" || echo "(returned non-zero)"
echo "--- unloading ---"; kextunload /tmp/$B; echo done.
