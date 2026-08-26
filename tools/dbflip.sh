#!/bin/sh
# Double-buffer via hardware PAGE-FLIP (CRTC_OFF_PITCH).  Rebuilds BOTH the kext (which
# now writes CRTC_OFF_PITCH on selector 0) and the GL bundle (-DGLD_DBUF), installs
# them, and runs the swap-chain animation.  Run on the Lombard under sudo.
#
# WATCH: the RGB triangle should sweep SMOOTHLY with NO stacking/residue and NO tearing
# (the flip loads at vblank).  On exit the desktop should return (detach restores the
# CRTC to the front buffer).
#
# RISK: this writes the live display-base register.  If the flip value were wrong the
# screen could show garbage until the app's detach restores it; if the app crashed
# without detaching you may need to switch resolution (or reboot) to restore scanout.
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext

echo "=== building kext (page-flip) ==="
( cd "$ROOT/kext" && make clean >/dev/null 2>&1; make kext ) || { echo "KEXT BUILD FAILED"; exit 1; }

echo "=== building GL bundle (-DGLD_DBUF) ==="
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF" bundle ) || { echo "BUNDLE BUILD FAILED"; exit 1; }

GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B || exit 1
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_dbanim" "$DIR/mach64_dbanim.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_dbanim"
kextunload /tmp/$B
echo "=== attach/flip log ==="
grep -aE "dbuf|DOUBLEBUFFER|render->back|DB-gate" /tmp/mach64gld.log | head
echo "=== DBflip alternation (batch_n / rendered_to / flip_to / MATCH?) ==="
grep -a "DBflip" /tmp/mach64gld.log | head -24
cp -f /tmp/mach64gld.log "$ROOT/dbflip-log.txt" 2>/dev/null
echo "--- done ---"
