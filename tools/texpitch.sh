#!/bin/sh
# Measure texture row pitch for small widths: solid per-quadrant blocks show a
# wrong pitch as a stairstep.  sudo sh texpitch.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_texpitch" "$DIR/mach64_texpitch.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_texpitch"
cp -f /tmp/mach64gld.log "$ROOT/texpitch-log.txt" 2>/dev/null
echo "=== uploads + emitted size_pitch per texture ==="
grep -aE "texture upload|mach64 tex: env=" /tmp/mach64gld.log 2>/dev/null | head -20
kextunload /tmp/$B; echo "--- done ---"
