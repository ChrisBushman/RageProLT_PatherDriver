#!/bin/sh
# Coexistence load test - loads the REAL device-matching kext.
#
# Our personality uses IOMatchCategory "Mach64GLAccelerator" (distinct from
# ATIRagePro's "IOAccelerator"), so IOKit starts us ALONGSIDE ATIRagePro on the
# same IOPCIDevice rather than replacing it -- the display should stay up.
# start() only maps the register/FB apertures and allocates buffers; it does
# NOT write any hardware registers, so it can't disturb ATIRagePro's display.
# Loaded from /tmp, so a reboot clears it regardless.
#
# Build first:  make -C kext kext
# Run as root:  sudo sh kext/coexist-test.sh
set -e
B=Mach64Accelerator.kext
DIR=`cd \`dirname "$0"\` && pwd`

echo "--- staging to /tmp (root:wheel) ---"
rm -rf /tmp/$B; cp -R "$DIR/$B" /tmp/$B; chown -R root:wheel /tmp/$B

echo "--- kextload -v (real GPU match, coexist category) ---"
kextload -v /tmp/$B

echo "--- kernel log: BAR map from start() ---"
dmesg | grep -i "Mach64Accelerator" | tail -12

echo "--- kextstat: should list com.local.Mach64Accelerator ---"
kextstat | grep -i mach64 || echo "  NOT LOADED (unexpected)"

echo "--- our IOService instance present? (want YES: it matched+started) ---"
if ioreg -w0 | grep -i Mach64Accelerator >/dev/null 2>&1; then
    echo "  OK: Mach64Accelerator STARTED on the device"
else
    echo "  no instance -- did not match/start (check BAR mapping in start())"
fi

echo "--- ATIRagePro still running? (display driver must be intact) ---"
if ioreg -w0 | grep -iE "IOATIRProAccelerator|ATIRagePro" | grep -vi catalogue >/dev/null 2>&1; then
    echo "  OK: ATIRagePro still present -- display untouched"
else
    echo "  WARNING: ATIRagePro not found!"
fi

echo "--- unloading ---"
kextunload /tmp/$B
echo "done.  (reboot also clears /tmp)"
