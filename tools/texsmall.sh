#!/bin/sh
# Validate w<8 texture uploads (linear-blit path): patterned 1x1/2x2/4x4/4x8 +
# an 8x8 control.  A wrong row stride shears the patterns.  sudo sh texsmall.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_texsmall" "$DIR/mach64_texsmall.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_texsmall"
cp -f /tmp/mach64gld.log "$ROOT/texsmall-log.txt" 2>/dev/null
echo "=== uploads (w<8 should show blit WxH = (w*h)x1 linear; 8x8 = 8x8) ==="
grep -aE "texture upload" /tmp/mach64gld.log 2>/dev/null | head
kextunload /tmp/$B; echo "--- done ---"
