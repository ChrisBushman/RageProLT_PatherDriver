#!/bin/sh
# revert-kext.sh -- roll the ATIRagePro kext back to a known-good backup.
#
# Default target: the .peek backup = the PEEK+UPLOAD kext (v2, md5 05b777e9...) -- textures work on
# small windows, no allocator.  This undoes the v3 allocator kext (which panicked: unlocked heap race
# + the sel-11/delete_texture repurpose that broke teardown).
#
#   sudo sh revert-kext.sh          # -> .peek (v2 peek+upload)  [recommended]
#   sudo sh revert-kext.sh orig     # -> .orig (STOCK, no patches at all)
#
# After it runs: SHUT DOWN and power back on (not just reboot) so the kext cache rebuilds.
set -e
K=/System/Library/Extensions/ATIRagePro.kext/Contents/MacOS/ATIRagePro

case "$1" in
  orig) SRC="$K.orig"; WHAT="STOCK (.orig, no patches)"; WANT="8fc97891fe7aa7605bb57e7820a4d643" ;;
  *)    SRC="$K.peek"; WHAT="peek+upload (.peek, v2)";    WANT="05b777e9b29417910bb93317ab8591b2" ;;
esac

[ -f "$SRC" ] || { echo "MISSING backup: $SRC"; exit 1; }
echo "current md5:  `md5 -q "$K"`"
cp "$SRC" "$K"
chown root:wheel "$K"
chmod 755 "$K"
touch /System/Library/Extensions
rm -f /System/Library/Extensions.mkext /System/Library/Extensions.kextcache 2>/dev/null || true

echo "reverted to $WHAT"
echo "installed md5: `md5 -q "$K"`  (expect $WANT)"
echo ""
echo ">>> SHUT DOWN and power back on for the kext to reload. <<<"
