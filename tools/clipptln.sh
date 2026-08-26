#!/bin/sh
# Probe/verify clipping of POINTS and LINES (companion to scissor).  Default build.  Runs
# mach64_clipptlntest: frame 1 scissor OFF (full row + lines), frame 2 scissor ON (clipped
# to a centred box).  Reveals whether GLEngine routes clipped points/lines through the flat
# slots (FLAT_POINTS/FLAT_LINES) or the element vectors (ELT_POINTS/ELT_LINES) and in what
# format, and the probe handlers best-effort render them so frame 2 also shows a picture.
# Screenshot frame 2: points only within the box x-span, lines trimmed to the box edges.
# Run on the Lombard under sudo:  sudo sh tools/clipptln.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
# Rebuilds the kext too (get_surface_size now returns the CRTC display height in *hi).
( cd "$ROOT/kext" && make clean >/dev/null 2>&1; make kext ) || { echo "KEXT BUILD FAILED"; exit 1; }
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; make bundle ) || exit 1
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_clipptlntest" "$DIR/mach64_clipptlntest.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_clipptlntest"
kextunload /tmp/$B
cp -f /tmp/mach64gld.log "$ROOT/clipptln-log.txt" 2>/dev/null
echo "=== screenshot frame 2: points only inside the box x-span, lines trimmed to the box ==="
echo "--- done (log in clipptln-log.txt) ---"
