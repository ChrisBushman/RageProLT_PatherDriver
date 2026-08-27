#!/bin/sh
# Path B, Phase 1 validation.  Builds our GL bundle with -DGLD_APPLE_PROBE so gldCreateContext
# ALSO opens Apple's ATIRagePro type-1 GL user client (IOATIRProGLContext) and reads get_config,
# then runs a minimal AGL context-creation app (no rendering/swap -> safe).  Confirms our
# GLEngine-loaded bundle can reach Apple's never-shipped kernel GL path -- the first real step
# of the Path B port.  Our own kext stays loaded (the probe is additive).  Run on the Lombard:
#   sudo sh tools/applephase1.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext

( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF -DGLD_APPLE_PROBE" bundle ) || exit 1
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$ROOT/gldriver/ATIRageProGLDriver.bundle" "$DST"; chown -R root:wheel "$DST"

( cd "$ROOT/kext" && make kext ) || { echo "KEXT BUILD FAILED"; exit 1; }
kextunload /tmp/$B 2>/dev/null
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log

cc -O2 -o "$DIR/mach64_ctxprobe" "$DIR/mach64_ctxprobe.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 || { echo "BUILD FAILED"; kextunload /tmp/$B; exit 1; }

echo "=== running ctxprobe (creates an accelerated context; no swap) ==="
"$DIR/mach64_ctxprobe" 2>&1 | tee "$ROOT/applephase1-out.txt"
kextunload /tmp/$B

echo "=== Phase 1 result: did our bundle open Apple's GL client + get_config? ==="
grep -E "apple-probe|gldCreateContext" /tmp/mach64gld.log 2>/dev/null
cp -f /tmp/mach64gld.log "$ROOT/applephase1-log.txt" 2>/dev/null
echo "--- done ---"
