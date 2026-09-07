#!/bin/sh
# install-upload-kext.sh -- deploy the texture-upload-patched ATIRagePro.kext (Path B).
#
# This kext = the peek patch (render-target read, needed by setup_target) PLUS a repurposed
# new_texture selector (GL sel 10) that does a plain VRAM memcpy, so the driver can upload texels
# WITHOUT the host-data blit (which wedges under Apple's PIO submit).  Source: ~/ATIRagePro.upload
# (md5 05b777e9b29417910bb93317ab8591b2).  Run as root:  sudo sh install-upload-kext.sh
#
# RECOVERY if the machine won't boot: boot into single-user (hold Cmd-S at chime), then:
#     /sbin/mount -uw /
#     cp /System/Library/Extensions/ATIRagePro.kext/Contents/MacOS/ATIRagePro.orig \
#        /System/Library/Extensions/ATIRagePro.kext/Contents/MacOS/ATIRagePro
#     touch /System/Library/Extensions ; reboot
set -e
K=/System/Library/Extensions/ATIRagePro.kext/Contents/MacOS/ATIRagePro
SRC=~chrisbushman/ATIRagePro.upload
[ -f "$SRC" ] || { echo "MISSING $SRC"; exit 1; }

# stock backup (keep if it already exists -- never overwrite it with a patched build)
[ -f "$K.orig" ] || cp -p "$K" "$K.orig"
# fallback: the currently-installed (peek-only) kext, so we can revert to it without a full reinstall
cp -p "$K" "$K.peek"

cp "$SRC" "$K"
chown root:wheel "$K"
chmod 755 "$K"

# invalidate the Extensions caches so the new binary is loaded on next boot
touch /System/Library/Extensions
rm -f /System/Library/Extensions.mkext /System/Library/Extensions.kextcache 2>/dev/null || true

echo "installed upload-patched kext.  md5: `md5 -q "$K"`  (expect 05b777e9b29417910bb93317ab8591b2)"
echo "backups: $K.orig (stock)  $K.peek (peek-only)"
echo ""
echo ">>> SHUT DOWN and power back on (not just reboot) for the kext to reload. <<<"
