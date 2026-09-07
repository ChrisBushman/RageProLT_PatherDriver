#!/bin/sh
# disable-mach64-kext.sh -- take OUR Mach64Accelerator.kext out of the boot set.
#
# Path B drives Apple's ATIRagePro.kext via the ATIRageProGLDriver.bundle and does NOT use our own
# accelerator kext.  But an old install.sh left Mach64Accelerator.kext in /S/L/E, so BOTH accelerators
# match the one Rage LT Pro -- the two-accelerator contention install.sh warns can hard-lock the box.
# This moves it aside so only Apple's kext (+ our bundle) drives the GPU.  Reversible.
#
#   sudo sh disable-mach64-kext.sh            # disable (move aside), rebuild cache
#   sudo sh disable-mach64-kext.sh --restore  # put it back
#
# After either: SHUT DOWN and power on so the kext set reloads.
set -e
SLE=/System/Library/Extensions
K="$SLE/Mach64Accelerator.kext"
OFF="$SLE/Mach64Accelerator.kext.disabled"
[ "`id -u`" = 0 ] || { echo "run as root: sudo sh disable-mach64-kext.sh"; exit 1; }

if [ "$1" = "--restore" ]; then
    if [ -d "$OFF" ]; then rm -rf "$K"; mv "$OFF" "$K"; echo "restored $K"; else echo "nothing to restore"; fi
else
    # try to unload the live instance first (best-effort; a reboot guarantees it either way)
    kextunload "$K" 2>/dev/null && echo "unloaded live Mach64Accelerator" || echo "(live unload skipped -- reboot will clear it)"
    if [ -d "$K" ]; then rm -rf "$OFF"; mv "$K" "$OFF"; echo "disabled -> $OFF"; else echo "already disabled/absent"; fi
fi

touch "$SLE"
rm -f "$SLE/Extensions.mkext" "$SLE/Extensions.kextcache" 2>/dev/null || true
kextcache -e 2>/dev/null || kextcache -k "$SLE" 2>/dev/null || echo "(mkext rebuilds on next boot)"
echo ""
echo ">>> SHUT DOWN and power on so the kext set reloads. <<<"
echo "Then: kextstat | grep -i mach64   should show NOTHING (only com.apple.ATIRagePro remains)."
