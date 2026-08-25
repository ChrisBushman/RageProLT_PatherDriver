#!/bin/sh
# De-risk the GUI-engine screen-to-screen blit.  Driver built -DGLD_BLIT_TEST
# self-blits front (0,0)300x200 -> (600,400) on glFinish.  sudo sh blittest.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_blittest" "$DIR/mach64_blittest.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_blittest"
cp -f /tmp/mach64gld.log "$ROOT/blittest-log.txt" 2>/dev/null
grep -aE 'blit-test|blit_surface|CRTC' /tmp/mach64gld.log 2>/dev/null | head
kextunload /tmp/$B; echo "--- done ---"
