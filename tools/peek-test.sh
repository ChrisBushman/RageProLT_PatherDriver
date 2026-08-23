#!/bin/sh
# First MMIO read test: load the coexist kext, run mach64_peek, unload.
# Reads are side-effect-free, so this is safe alongside ATIRagePro.
#
# Build first:  make -C kext kext && make -C tools
# Run as root:  sudo sh tools/peek-test.sh
set -e
DIR=`cd \`dirname "$0"\` && pwd`
ROOT=`cd "$DIR/.." && pwd`
B=Mach64Accelerator.kext

echo "--- staging + loading kext ---"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B
kextload /tmp/$B

echo "--- mach64_peek (first register read) ---"
"$DIR/mach64_peek" || echo "(peek returned non-zero)"

echo "--- unloading ---"
kextunload /tmp/$B
echo "done."
