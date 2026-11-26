#!/bin/sh
# Run as root on the G4: undo install-tgl.sh (restores kext binaries, removes /Library/TGL and the Tiger GL bundles).
set -e
E=/System/Library/Extensions; B=$(cat /Library/TGL-backup/LAST)
cat $B/GeForce.bin > $E/GeForce.kext/Contents/MacOS/GeForce
cat $B/compat104.bin > $E/compat104.kext/Contents/MacOS/compat104
rm -rf /Library/TGL $E/GeForceFXGLDriver.bundle $E/GeForce3GLDriver.bundle $E/GeForce2MXGLDriver.bundle
touch $E; echo "REVERTED from $B. Reboot."
