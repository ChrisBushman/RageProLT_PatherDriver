#!/bin/sh
# End-to-end double buffering via the PIXEL FORMAT (NORMAL driver build, no flag).
# sudo sh dbufanim.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_dbufanim" "$DIR/mach64_dbufanim.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_dbufanim" 2>&1 | tee /tmp/dbufanim.out
cp -f /tmp/dbufanim.out  "$ROOT/dbufanim-testout.txt" 2>/dev/null
cp -f /tmp/mach64gld.log "$ROOT/dbufanim-log.txt"     2>/dev/null
echo "=== pf state + dbuf plumbing ==="
grep -aE 'AGL_DOUBLEBUFFER|DOUBLEBUFFER|dbuf|present|texheap' /tmp/mach64gld.log /tmp/dbufanim.out 2>/dev/null | head -20
kextunload /tmp/$B; echo "--- done ---"
