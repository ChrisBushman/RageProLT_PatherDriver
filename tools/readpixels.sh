#!/bin/sh
# glReadPixels discovery/verify.  Default build.  Renders clear + white quad, reads a clear
# pixel and a quad pixel, prints them + the gld* entry points that fired.  Run on the Lombard:
#   sudo sh tools/readpixels.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF" bundle ) || exit 1
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_readpixels" "$DIR/mach64_readpixels.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
RESULT="$ROOT/readpixels-result.txt"
"$DIR/mach64_readpixels" 2>&1 | tee "$RESULT"
kextunload /tmp/$B
echo "=== gld entry points around the readback (unique) ===" | tee -a "$RESULT"
grep -oE "^gld[A-Za-z]+" /tmp/mach64gld.log | sort -u | tee -a "$RESULT"
echo "=== kext read_buffer (sel 7) calls from the system log ===" | tee -a "$RESULT"
grep "read_buffer" /var/log/system.log 2>/dev/null | tail -6 | tee -a "$RESULT"
cp -f /tmp/mach64gld.log "$ROOT/readpixels-log.txt" 2>/dev/null
echo "--- done ---"
