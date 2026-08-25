#!/bin/sh
# Delete-then-recreate -> validate the free-list VRAM allocator reclaims a
# deleted texture's region (top-of-heap reset + mid-heap hole reuse).
# The signal is the upload log's "-> VRAM 0x<off>" offsets: after a delete the
# next texture must REUSE an offset, not climb.  sudo sh texreuse.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_texreuse" "$DIR/mach64_texreuse.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_texreuse"
cp -f /tmp/mach64gld.log "$ROOT/texreuse-log.txt" 2>/dev/null
echo "=== texture uploads (watch the VRAM offsets reuse across deletes) ==="
grep -aE "texture upload|free list full|heap full" /tmp/mach64gld.log 2>/dev/null | head -30
kextunload /tmp/$B; echo "--- done ---"
