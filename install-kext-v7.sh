#!/bin/sh
# install-kext-v7.sh -- deploy the v7 ATIRagePro kext (Path B).
#
# v7 = v5 (peek sel-14 + multiplexed sel-10 upload/alloc/free + 64KB cmdbuf) PLUS op=3 (AGP alloc-only) + op=5 (chunked AGP upload)
# added to the sel-10 handler: allocate a texture in SYSTEM RAM via the GART aperture
# (new_agp_texture@0xf64c), memcpy the ARGB texels into its kernel VA (tex[0x94]), GART-map it
# (addToGART@0x4398) -- all under accel[0x544] -- returning {gart_off(=TEX_0_OFF), token, handle}.
# Lets FULLSCREEN textures work despite cramped VRAM.  ONE edit to the proven handler (0x7208 ->
# dispatch), body in the dead stock tail [0x72d8,0x7388); op0/op1/op2 byte-identical to v5.
# Source: ~/ATIRagePro.v7  (md5 1fbe201f91a55e32e56561cd263580ef).  Run as root:
#   sudo sh install-kext-v7.sh
#
# Fallbacks (revert-kext.sh):  $K.orig = STOCK,  $K.peek = v2 peek+upload.  Both preserved.
# RECOVERY if it won't boot: single-user (Cmd-S), /sbin/mount -uw / , cp the .orig back, shutdown.
set -e
K=/System/Library/Extensions/ATIRagePro.kext/Contents/MacOS/ATIRagePro
SRC=~chrisbushman/ATIRagePro.v7
WANT=1fbe201f91a55e32e56561cd263580ef
[ -f "$SRC" ] || { echo "MISSING $SRC"; exit 1; }
[ "`id -u`" = 0 ] || { echo "run as root: sudo sh install-kext-v7.sh"; exit 1; }
[ "`md5 -q "$SRC"`" = "$WANT" ] || { echo "SRC md5 mismatch (`md5 -q "$SRC"`); refusing"; exit 1; }

# stock backup: create once, NEVER overwrite with a patched build
[ -f "$K.orig" ] || cp -p "$K" "$K.orig"
# v2 fallback: keep an existing .peek; only seed it if missing AND current isn't already a patched build
if [ ! -f "$K.peek" ] && [ "`md5 -q "$K"`" != "$WANT" ]; then cp -p "$K" "$K.peek"; fi

cp "$SRC" "$K"
chown root:wheel "$K"; chmod 755 "$K"
touch /System/Library/Extensions
rm -f /System/Library/Extensions.mkext /System/Library/Extensions.kextcache 2>/dev/null || true

echo "installed v7 kext.  md5: `md5 -q "$K"`  (expect $WANT)"
echo "backups: $K.orig (stock)  $K.peek (v2 peek+upload)"
echo ""
echo ">>> SHUT DOWN and power back on (not just reboot) for the kext to reload. <<<"
