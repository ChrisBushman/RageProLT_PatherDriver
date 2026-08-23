#!/bin/sh
# Safe non-matching load test.
#
# Loads Mach64Accelerator-safe.kext, whose IOPCIMatch targets a device that is
# NOT present on this machine, so the kext's module init runs and its classes
# register, but it matches NO device and therefore cannot touch the live
# display.  Loaded from /tmp, so a reboot fully clears it either way.
#
# Build the bundle first:   make -C kext safe-bundle
# Then run as root:         sudo sh kext/load-safe.sh
set -e
B=Mach64Accelerator-safe.kext
DIR=`cd \`dirname "$0"\` && pwd`

echo "--- staging to /tmp (root:wheel) ---"
rm -rf /tmp/$B
cp -R "$DIR/$B" /tmp/$B
chown -R root:wheel /tmp/$B

echo "--- kextload -v (matches nothing here) ---"
kextload -v /tmp/$B

echo "--- kextstat: should list com.local.Mach64Accelerator ---"
kextstat | grep -i mach64 || echo "  NOT LOADED (unexpected)"

echo "--- ioreg: should find NO Mach64Accelerator instance (matched nothing = correct) ---"
if ioreg -w0 | grep -i Mach64Accelerator >/dev/null 2>&1; then
    echo "  WARNING: an instance exists -- it matched something!"
else
    echo "  OK: no instance -- matched nothing, display untouched"
fi

echo "--- kextunload ---"
kextunload /tmp/$B
echo "done.  (reboot also clears anything left in /tmp)"
