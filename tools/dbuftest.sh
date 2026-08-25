#!/bin/sh
# Double-buffer bring-up: driver built -DGLD_DBUF_TEST renders to a back buffer and
# blits back->front on finish/swap.  Vehicle: the known-good texprim 3D test.
# Expect the SAME four clean squares as single-buffered (Picture 17).  sudo sh dbuftest.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_dbufscene" "$DIR/mach64_dbufscene.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_dbufscene"
cp -f /tmp/mach64gld.log "$ROOT/dbuftest-log.txt" 2>/dev/null
grep -aE 'dbuf|texheap|present' /tmp/mach64gld.log 2>/dev/null | head
kextunload /tmp/$B; echo "--- done ---"
