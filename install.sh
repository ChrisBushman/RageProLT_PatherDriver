#!/bin/sh
# install.sh - PERMANENT install of the Rage LT Pro OpenGL driver on Mac OS X 10.3 (Panther/PPC).
#
# Installs two pieces into /System/Library/Extensions so they load automatically at every boot,
# replacing the manual per-run test install (rm -rf / cp -R / kextload):
#   1. Mach64Accelerator.kext  -- the kernel accelerator (register access, DMA ring, present).
#      Its Info.plist IOPCIMatch (0x4c491002 / 0x4c421002 / 0x4c501002) auto-matches the LT Pro at
#      boot; IOMatchCategory "Mach64GLAccelerator" is distinct from Apple's ATIRagePro category, so
#      the two coexist on the one IOPCIDevice (Apple keeps 2D/scanout; we own the GL/3D engine).
#   2. ATIRageProGLDriver.bundle -- the userspace GLEngine renderer plugin.  GLEngine scans
#      /S/L/E/*GLDriver.bundle; ours IS the Rage Pro renderer (advertises renderer id 0x2540).
#
# Also disables Apple's never-finished Rage Pro GL plugin so GLEngine can't route to it (two
# accelerators contending on the one device hard-locks the box).
#
# Run on the Lombard as root:   sudo sh install.sh
set -e
ROOT=`cd \`dirname "$0"\` && pwd`
SLE=/System/Library/Extensions
KEXT=Mach64Accelerator.kext
BUNDLE=ATIRageProGLDriver.bundle

[ "`id -u`" = 0 ] || { echo "ERROR: run as root:  sudo sh install.sh"; exit 1; }

echo "== [1/6] building the release driver (bundle -DGLD_DBUF, kext) =="
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF" bundle ) || { echo "BUNDLE BUILD FAILED"; exit 1; }
( cd "$ROOT/kext" && make clean >/dev/null 2>&1; make kext ) || { echo "KEXT BUILD FAILED"; exit 1; }

echo "== [2/6] installing the GL renderer bundle -> $SLE/$BUNDLE =="
rm -rf "$SLE/$BUNDLE"
cp -R "$ROOT/gldriver/$BUNDLE" "$SLE/$BUNDLE"
chown -R root:wheel "$SLE/$BUNDLE"; chmod -R 755 "$SLE/$BUNDLE"

echo "== [3/6] installing the accelerator kext -> $SLE/$KEXT =="
kextunload "$SLE/$KEXT" 2>/dev/null || true
rm -rf "$SLE/$KEXT"
cp -R "$ROOT/kext/$KEXT" "$SLE/$KEXT"
chown -R root:wheel "$SLE/$KEXT"; chmod -R 755 "$SLE/$KEXT"

echo "== [4/6] disabling Apple's Rage Pro GL plugin (avoid two accelerators) =="
DISABLED=0
for p in "$SLE/ATIRageProGA.plugin" \
         "$SLE/ATIRagePro.kext/Contents/PlugIns/ATIRageProGA.plugin"; do
    if [ -e "$p" ] && [ ! -e "$p.off" ]; then mv "$p" "$p.off"; echo "  disabled $p"; DISABLED=1; fi
done
[ "$DISABLED" = 0 ] && echo "  (Apple's Rage Pro GL plugin already absent/disabled -- ok)"

echo "== [5/6] rebuilding the kext cache so the kext loads at boot =="
touch "$SLE"                                   # bump mtime -> mkext rebuild on next boot
kextcache -e 2>/dev/null || kextcache -k "$SLE" 2>/dev/null || \
    echo "  (kextcache not run; the touched $SLE rebuilds the mkext on next boot)"

echo "== [6/6] loading the kext now (so no reboot is needed to use it) =="
if kextload "$SLE/$KEXT" 2>&1; then echo "  loaded and matched the LT Pro"; else
    echo "  WARNING: kextload reported an issue -- check the output above (it will still load at boot)"; fi

echo
echo "DONE.  The Rage LT Pro OpenGL driver is installed permanently and loads at boot."
echo "Accelerated GL apps (fullscreen and windowed) now run through it."
echo "To remove:  sudo sh $ROOT/uninstall.sh"
