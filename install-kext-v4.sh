#!/bin/sh
# install-kext-v4.sh -- deploy the v4 ATIRagePro kext (Path B).
#
# v4 = peek (sel 14) + MULTIPLEXED new_texture (sel 10): op=0 upload, op=1 alloc, op=2 free.
# The alloc/free wrap Apple's shared-heap best-fit in the accel[0x544] mutex (fixes the v3 race), and
# sel 11 (delete_texture) is LEFT STOCK (v3 broke it -> teardown panics).  Source: ~/ATIRagePro.v4
# (md5 421c9c2542a7ef3d42e1a12febcda2dd).  Run as root:  sudo sh install-kext-v4.sh
#
# Fallbacks (used by revert-kext.sh):  $K.orig = STOCK,  $K.peek = v2 peek+upload.
# This script PRESERVES both -- it never overwrites a good .orig/.peek with a patched build.
#
# RECOVERY if it won't boot: single-user (hold Cmd-S), then
#     /sbin/mount -uw /
#     cp .../ATIRagePro.kext/Contents/MacOS/ATIRagePro.orig \
#        .../ATIRagePro.kext/Contents/MacOS/ATIRagePro
#     touch /System/Library/Extensions ; shutdown -h now
set -e
K=/System/Library/Extensions/ATIRagePro.kext/Contents/MacOS/ATIRagePro
SRC=~chrisbushman/ATIRagePro.v4
WANT=421c9c2542a7ef3d42e1a12febcda2dd
[ -f "$SRC" ] || { echo "MISSING $SRC"; exit 1; }
[ "`id -u`" = 0 ] || { echo "run as root: sudo sh install-kext-v4.sh"; exit 1; }
[ "`md5 -q "$SRC"`" = "$WANT" ] || { echo "SRC md5 mismatch (`md5 -q "$SRC"`); refusing"; exit 1; }

# stock backup: create once, NEVER overwrite with a patched build
[ -f "$K.orig" ] || cp -p "$K" "$K.orig"
# v2 fallback: keep an existing .peek (it's the known-good peek+upload); only seed it if missing AND
# the current kext is NOT already a patched build (md5 != v4) so we don't capture v4 into .peek.
if [ ! -f "$K.peek" ] && [ "`md5 -q "$K"`" != "$WANT" ]; then cp -p "$K" "$K.peek"; fi

cp "$SRC" "$K"
chown root:wheel "$K"; chmod 755 "$K"
touch /System/Library/Extensions
rm -f /System/Library/Extensions.mkext /System/Library/Extensions.kextcache 2>/dev/null || true

echo "installed v4 kext.  md5: `md5 -q "$K"`  (expect $WANT)"
echo "backups: $K.orig (stock)  $K.peek (v2 peek+upload)"
echo ""
echo ">>> SHUT DOWN and power back on (not just reboot) for the kext to reload. <<<"
