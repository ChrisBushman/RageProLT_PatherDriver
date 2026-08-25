#!/bin/sh
# Recover the GL_FOG-enable gli offset.  Rebuilds the GL driver WITH -DGLD_FOGSCAN,
# installs it, runs mach64_fogscan (fog-off frame then fog-on frame), and prints the
# FOGSCAN log lines.  This is a TEXT scan -- read the log, no picture is produced.
# Run on the Lombard under sudo:  sudo sh tools/fogscan.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext

# Build the renderer bundle with the fog-scan instrumentation compiled in.
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_FOGSCAN" bundle ) || exit 1

GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_fogscan" "$DIR/mach64_fogscan.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_fogscan"
kextunload /tmp/$B
echo "=== FOGSCAN (state deltas + per-vertex fog factors) ==="
grep FOGSCAN /tmp/mach64gld.log
echo "--- done ---"
