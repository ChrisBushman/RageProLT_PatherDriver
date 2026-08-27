#!/bin/sh
# Recover the GL_CULL_FACE / glCullFace / glFrontFace gli offsets.  Reuses the wide gli-diff
# scan (GLD_SCISSCAN): builds the renderer with it, runs mach64_culltest (a cull-OFF frame
# then a cull-GL_BACK frame), and prints the delta -- the changed word(s) are the cull state.
# Run on the Lombard under sudo:  sudo sh tools/cullscan.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_SCISSCAN" bundle ) || exit 1
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_culltest" "$DIR/mach64_culltest.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_culltest"
kextunload /tmp/$B
RESULT="$ROOT/cullscan-result.txt"
{ echo "=== cull-face gli delta (GL_CULL_FACE off -> on GL_BACK) ==="
  grep -A30 "SCISSCAN delta" /tmp/mach64gld.log | head -40
  echo "--- done ---"; } | tee "$RESULT"
echo "(wrote $RESULT)"
