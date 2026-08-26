#!/bin/sh
# Recover the GL_SCISSOR_TEST enable + box gli offsets, AND probe feasibility in one run.
# Rebuilds the GL driver WITH -DGLD_SCISSCAN, installs it, runs mach64_scissortest
# (scissor-OFF frame then scissor-ON frame with a distinctive box), prints the SCISSCAN
# delta lines.  The app also renders on screen (frame 2 = scissor ON): screenshot it --
# a FULL white quad = HW scissor feasible, a box = GLEngine clipped host-side.
# Run on the Lombard under sudo:  sudo sh tools/scisscan.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext

# Build the renderer bundle with the scissor-scan instrumentation compiled in.
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_SCISSCAN" bundle ) || exit 1

GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_scissortest" "$DIR/mach64_scissortest.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_scissortest"
kextunload /tmp/$B
# Write a clean result file (the SCISSCAN delta) AND keep the full driver log, so the
# offsets can be read straight off disk with no copy/paste.
RESULT="$ROOT/scisscan-result.txt"
{
  echo "=== SCISSCAN (enable + box deltas: look for 101/203/305/407, right=406, top=610) ==="
  grep -A60 "SCISSCAN delta" /tmp/mach64gld.log | head -80
  echo "--- done ---"
} | tee "$RESULT"
cp -f /tmp/mach64gld.log "$ROOT/scisscan-log.txt" 2>/dev/null
echo "(wrote $RESULT and scisscan-log.txt)"
