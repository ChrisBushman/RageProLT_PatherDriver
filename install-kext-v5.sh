#!/bin/sh
# install-kext-v5.sh -- deploy the v5 ATIRagePro kext (Path B).
#
# v5 = v4 (peek sel-14 + multiplexed sel-10 upload/alloc/free) PLUS the command-buffer enlarged
# 4KB -> 64KB (two `li r,0x1000`->`lis r,1` patches @ vaddr 0x5164 + 0x5fe4).  Lifts the per-frame
# geometry ceiling from ~15 to ~200 triangles, still ONE present/frame (no wedge).  The driver
# auto-adapts (its batch limit reads the live map size).  Source: ~/ATIRagePro.v5
# (md5 3803034d703f3439aaaea3dd200a9565).  Run as root:  sudo sh install-kext-v5.sh
#
# Fallbacks (revert-kext.sh):  $K.orig = STOCK,  $K.peek = v2 peek+upload.  Both preserved.
# RECOVERY if it won't boot: single-user (Cmd-S), /sbin/mount -uw / , cp the .orig back, shutdown.
set -e
K=/System/Library/Extensions/ATIRagePro.kext/Contents/MacOS/ATIRagePro
SRC=~chrisbushman/ATIRagePro.v5
WANT=3803034d703f3439aaaea3dd200a9565
[ -f "$SRC" ] || { echo "MISSING $SRC"; exit 1; }
[ "`id -u`" = 0 ] || { echo "run as root: sudo sh install-kext-v5.sh"; exit 1; }
[ "`md5 -q "$SRC"`" = "$WANT" ] || { echo "SRC md5 mismatch (`md5 -q "$SRC"`); refusing"; exit 1; }

# stock backup: create once, NEVER overwrite with a patched build
[ -f "$K.orig" ] || cp -p "$K" "$K.orig"
# v2 fallback: keep an existing .peek; only seed it if missing AND current isn't already a patched build
if [ ! -f "$K.peek" ] && [ "`md5 -q "$K"`" != "$WANT" ]; then cp -p "$K" "$K.peek"; fi

cp "$SRC" "$K"
chown root:wheel "$K"; chmod 755 "$K"
touch /System/Library/Extensions
rm -f /System/Library/Extensions.mkext /System/Library/Extensions.kextcache 2>/dev/null || true

echo "installed v5 kext.  md5: `md5 -q "$K"`  (expect $WANT)"
echo "backups: $K.orig (stock)  $K.peek (v2 peek+upload)"
echo ""
echo ">>> SHUT DOWN and power back on (not just reboot) for the kext to reload. <<<"
