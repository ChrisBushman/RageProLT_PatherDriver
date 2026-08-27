#!/bin/sh
# Recover the glShadeModel (GL_FLAT/GL_SMOOTH) gli offset.  Reuses the wide gli-diff scan
# (GLD_SCISSCAN, 0x5000-0x7000): builds the renderer with it, runs mach64_shadetest (a
# GL_SMOOTH frame then a GL_FLAT frame changing nothing else), and prints the delta -- the
# changed word(s) are where GLEngine stores the shade model.
# Run on the Lombard under sudo:  sudo sh tools/shadescan.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_SCISSCAN" bundle ) || exit 1
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_shadetest" "$DIR/mach64_shadetest.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_shadetest"
kextunload /tmp/$B
RESULT="$ROOT/shadescan-result.txt"
{ echo "=== shade-model gli delta (GL_SMOOTH -> GL_FLAT) ==="
  grep -A30 "SCISSCAN delta" /tmp/mach64gld.log | head -40
  echo "--- done ---"; } | tee "$RESULT"
echo "(wrote $RESULT)"
