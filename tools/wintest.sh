#!/bin/sh
# WINDOWED coexistence probe.  Installs the driver + kext, then runs mach64_wintest, which binds
# an ACCELERATED double-buffered AGL context to a normal 400x300 window and renders a spinning
# triangle into it for ~4s.  The driver (env MACH64_WIN_RECT, set by the tool) renders off-screen
# and BLITs the window rect into the LIVE front buffer each swap -- NO CRTC page-flip -- so the
# WindowServer keeps compositing the desktop while our bus-master runs.  The test: does the engine
# coexist (triangle spins in the window, desktop intact) or HARD-LOCK?  There is no CRTC hijack
# here (no page-flip), so no frozen-frame; a hard lock would need a reboot, but nothing else does.
# Run on the Lombard:  sudo sh tools/wintest.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext

# driver bundle (-DGLD_DBUF so it advertises the accelerated DB config a windowed app wants)
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF" bundle ) || exit 1
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$ROOT/gldriver/ATIRageProGLDriver.bundle" "$DST"; chown -R root:wheel "$DST"

# kext
( cd "$ROOT/kext" && make kext ) || { echo "KEXT BUILD FAILED"; exit 1; }
kextunload /tmp/$B 2>/dev/null
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log

cc -O2 -o "$DIR/mach64_wintest" "$DIR/mach64_wintest.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 || { echo "BUILD FAILED"; kextunload /tmp/$B; exit 1; }

echo "=== running windowed probe (no swap; safe) ==="
"$DIR/mach64_wintest" 2>&1 | tee "$ROOT/wintest-out.txt"
kextunload /tmp/$B

echo "=== driver: windowed attach + raw descriptor dump ==="
grep -E "gldAttachDrawable|drawable\[|\[cgs\]|\[win\]" /tmp/mach64gld.log 2>/dev/null | head -20
cp -f /tmp/mach64gld.log "$ROOT/wintest-log.txt" 2>/dev/null
echo "--- done ---"
