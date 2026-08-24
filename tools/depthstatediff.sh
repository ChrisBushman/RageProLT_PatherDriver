#!/bin/sh
# Dump the gli context for 3 GL depth configs so we can diff and locate where
# GLEngine stores glDepthFunc / glDepthMask / GL_DEPTH_TEST enable.
#   sudo sh depthstatediff.sh   -> ~/RageProLT/gli_df{0,1,2}.txt
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
cc -o "$DIR/mach64_depthstate" "$DIR/mach64_depthstate.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
for cfg in 0 1 2; do
  echo "--- gli dump cfg=$cfg ---"
  gdb -batch -x "$DIR/glidump.gdb" --args "$DIR/mach64_depthstate" $cfg > "$ROOT/gli_df$cfg.txt" 2>&1
  echo "  wrote gli_df$cfg.txt ($(grep -c 0x "$ROOT/gli_df$cfg.txt") lines)"
done
kextunload /tmp/$B; echo "--- done ---"
