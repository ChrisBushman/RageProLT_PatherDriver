#!/bin/sh
# uninstall.sh - remove the Rage LT Pro OpenGL driver installed by install.sh and restore Apple's
# stock configuration.  Run on the Lombard as root:   sudo sh uninstall.sh
set -e
ROOT=`cd \`dirname "$0"\` && pwd`
SLE=/System/Library/Extensions
KEXT=Mach64Accelerator.kext
BUNDLE=ATIRageProGLDriver.bundle

[ "`id -u`" = 0 ] || { echo "ERROR: run as root:  sudo sh uninstall.sh"; exit 1; }

echo "== unloading + removing the accelerator kext =="
kextunload "$SLE/$KEXT" 2>/dev/null || true
rm -rf "$SLE/$KEXT"

echo "== removing the GL renderer bundle =="
rm -rf "$SLE/$BUNDLE"

echo "== restoring Apple's Rage Pro GL plugin =="
RESTORED=0
for p in "$SLE/ATIRageProGA.plugin.off" \
         "$SLE/ATIRagePro.kext/Contents/PlugIns/ATIRageProGA.plugin.off"; do
    if [ -e "$p" ]; then mv "$p" "${p%.off}"; echo "  restored ${p%.off}"; RESTORED=1; fi
done
[ "$RESTORED" = 0 ] && echo "  (no install-disabled Apple plugin found; if you moved one aside manually, restore it by hand)"

echo "== rebuilding the kext cache =="
touch "$SLE"
kextcache -e 2>/dev/null || kextcache -k "$SLE" 2>/dev/null || \
    echo "  (kextcache not run; the touched $SLE rebuilds the mkext on next boot)"

echo
echo "DONE.  Our driver is removed.  A reboot returns the machine to Apple's stock Rage Pro path."
