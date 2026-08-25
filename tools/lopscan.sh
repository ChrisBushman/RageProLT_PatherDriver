#!/bin/sh
# Recover the GL_COLOR_LOGIC_OP enable + glLogicOp value gli offsets.  Rebuilds the GL
# driver WITH -DGLD_FOGSCAN (its state-cluster delta dump is generic), runs
# mach64_lopscan (3 frames toggling logic-op state), and prints the state-delta lines.
# TEXT scan -- read the log, no picture.  Run on the Lombard under sudo.
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext

( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_FOGSCAN" bundle ) || exit 1

GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_lopscan" "$DIR/mach64_lopscan.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_lopscan"
kextunload /tmp/$B
echo "=== state-cluster deltas (enable + glLogicOp value words) ==="
grep -nE "FOGSCAN delta|gli\+0x" /tmp/mach64gld.log
echo "--- done ---"
