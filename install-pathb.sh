#!/bin/sh
# install-pathb.sh - PATH B install of the Rage Pro OpenGL renderer on Mac OS X 10.3 (Panther/PPC).
#
# Unlike the old install.sh (which installed OUR Mach64Accelerator.kext alongside Apple's driver),
# Path B drops our kext entirely: the ATIRageProGLDriver.bundle drives APPLE's shipped
# ATIRagePro.kext directly (the empty GL socket its IOGLBundleName="ATIRageProGLDriver" points at).
# So this installs ONLY the userspace bundle -- no kext, no kextcache, no reboot, and it does NOT
# touch Apple's ATIRagePro.kext (we USE it).  A GL app picks up the new bundle the next time it
# initializes OpenGL (GLEngine dlopen's /S/L/E/<IOGLBundleName>.bundle fresh each launch).
#
# Run on the Lombard as root:   sudo sh install-pathb.sh
#   ...then launch any GL app and read the trace:   cat /tmp/mach64gld.log
#
# Revert at any time:            sudo sh install-pathb.sh --uninstall
set -e
ROOT=`cd \`dirname "$0"\` && pwd`
SLE=/System/Library/Extensions
BUNDLE=ATIRageProGLDriver.bundle
TARGET="$SLE/$BUNDLE"
BACKUP="$SLE/$BUNDLE.orig"
LOG=/tmp/mach64gld.log

[ "`id -u`" = 0 ] || { echo "ERROR: run as root:  sudo sh install-pathb.sh"; exit 1; }

# ---- uninstall: restore Apple's original (or just remove ours) --------------------
if [ "$1" = "--uninstall" ] || [ "$1" = "-u" ]; then
    echo "== uninstalling our bundle =="
    rm -rf "$TARGET"
    if [ -e "$BACKUP" ]; then mv "$BACKUP" "$TARGET"; echo "  restored Apple's original $BUNDLE"; else
        echo "  removed $TARGET (Apple shipped none here originally)"; fi
    echo "DONE.  Relaunch your GL app to fall back to the previous renderer."
    exit 0
fi

# ---- 1. build the Path-B bundle (default Makefile now has -DGLD_APPLE_PROBE) ------
echo "== [1/4] building the Path-B GL bundle =="
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; make bundle ) \
    || { echo "BUNDLE BUILD FAILED -- fix the build before installing"; exit 1; }
[ -x "$ROOT/gldriver/$BUNDLE/Contents/MacOS/ATIRageProGLDriver" ] \
    || { echo "ERROR: built bundle exec missing"; exit 1; }

# ---- 2. back up whatever is at the target (once), then install ours ---------------
echo "== [2/4] installing $BUNDLE -> $SLE =="
if [ -e "$TARGET" ] && [ ! -e "$BACKUP" ]; then
    mv "$TARGET" "$BACKUP"; echo "  backed up the existing bundle -> $BACKUP"
fi
rm -rf "$TARGET"
cp -R "$ROOT/gldriver/$BUNDLE" "$TARGET"
chown -R root:wheel "$TARGET"; chmod -R 755 "$TARGET"
echo "  installed: `file "$TARGET/Contents/MacOS/ATIRageProGLDriver" | sed 's/.*: //'`"

# ---- 3. confirm Apple's ATIRagePro.kext is present (we drive it) ------------------
echo "== [3/4] checking Apple's ATIRagePro.kext (Path B drives it, does NOT replace it) =="
if [ -d "$SLE/ATIRagePro.kext" ]; then echo "  ATIRagePro.kext present -- good"; else
    echo "  WARNING: $SLE/ATIRagePro.kext not found -- the bundle has no accelerator to open!"; fi

# ---- 4. fresh log so the next run is clean ---------------------------------------
echo "== [4/4] clearing the trace log =="
rm -f "$LOG"; echo "  $LOG cleared (recreated on the next GL app launch)"

echo
echo "DONE (Path B).  No reboot needed."
echo "  1. Launch a GL app (e.g. an OpenGL screensaver, or the SDK LA_GL test)."
echo "  2. Read the trace:   cat $LOG"
echo "     Look for [apple-probe] set_surface ... SURFACE BOUND, [pathb] mapped cmd(type1),"
echo "     and [pathb] write_lock backing=0x...  (tells us VRAM vs system-memory backing)."
echo "  Revert:  sudo sh $ROOT/install-pathb.sh --uninstall"
