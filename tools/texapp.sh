#!/bin/sh
# Texture investigation: log the gld texture-call flow + dump the textured vertex.
#   sudo sh texapp.sh   (log tail printed; full log in /tmp/mach64gld.log)
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_texapp" "$DIR/mach64_texapp.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_texapp"
cp -f /tmp/mach64gld.log "$ROOT/texapp-log.txt" 2>/dev/null
echo "=== texture calls + TEXHUNT (full log in ~/RageProLT/texapp-log.txt) ==="
grep -iE "Texture|TEXHUNT|v0\+0x" /tmp/mach64gld.log 2>/dev/null | head -40
kextunload /tmp/$B; echo "--- done ---"
