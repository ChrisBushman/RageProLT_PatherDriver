#!/bin/sh
# Dump the gli context for GL_TEXTURE_2D enabled(0) vs disabled(1) so we can diff
# and locate where GLEngine stores the GL_TEXTURE_2D enable bit.
#   sudo sh texstatediff.sh   -> ~/RageProLT/gli_tx{0,1}.txt
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
cc -o "$DIR/mach64_texstate" "$DIR/mach64_texstate.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
for cfg in 0 1; do
  echo "--- gli dump cfg=$cfg ---"
  gdb -batch -x "$DIR/glidump.gdb" --args "$DIR/mach64_texstate" $cfg > "$ROOT/gli_tx$cfg.txt" 2>&1
  echo "  wrote gli_tx$cfg.txt ($(grep -c 0x "$ROOT/gli_tx$cfg.txt") lines)"
done
kextunload /tmp/$B; echo "--- done ---"
