#!/bin/sh
# Scratch-register write test: load kext, run mach64_poke, unload.
# Writes/reads SCRATCH_REG1 (no hardware side effects) and restores it, so it's
# safe alongside ATIRagePro.
#
# Build first:  make -C kext kext && make -C tools
# Run as root:  sudo sh tools/scratch-test.sh
set -e
DIR=`cd \`dirname "$0"\` && pwd`
ROOT=`cd "$DIR/.." && pwd`
B=Mach64Accelerator.kext

echo "--- staging + loading kext ---"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B
kextload /tmp/$B

echo "--- mach64_poke (scratch-register write/read) ---"
"$DIR/mach64_poke" || echo "(poke returned non-zero)"

echo "--- unloading ---"
kextunload /tmp/$B
echo "done."
