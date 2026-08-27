#!/bin/sh
# Identify the glReadPixels dispatch function our HW renderer uses.  Installs the driver bundle +
# kext (so GL_RENDERER is our accelerated driver), then runs mach64_rpdisp, which reads the
# per-context glReadPixels slot [ctx+0x3b8] and dladdr's it (+ scans the context for GLEngine code
# pointers).  Pure userspace reads -> safe (no reboot risk).  Run in the console session:
#   sudo sh tools/rpdisp.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext

( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF" bundle ) || exit 1
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$ROOT/gldriver/ATIRageProGLDriver.bundle" "$DST"; chown -R root:wheel "$DST"

( cd "$ROOT/kext" && make kext ) || { echo "KEXT BUILD FAILED"; exit 1; }
kextunload /tmp/$B 2>/dev/null
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B

cc -O2 -o "$DIR/mach64_rpdisp" "$DIR/mach64_rpdisp.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 | grep -viE "warning:" ; true
[ -x "$DIR/mach64_rpdisp" ] || { echo "BUILD FAILED"; kextunload /tmp/$B; exit 1; }

echo "=== running rpdisp (reads glReadPixels dispatch slot) ==="
"$DIR/mach64_rpdisp" 2>&1 | tee "$ROOT/rpdisp-out.txt"
kextunload /tmp/$B
echo "--- done; output in rpdisp-out.txt ---"
