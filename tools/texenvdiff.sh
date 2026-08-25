#!/bin/sh
# Dump the gli context for GL_MODULATE(0) vs GL_REPLACE(1) vs GL_DECAL(2), so we
# can diff and locate where GLEngine stores GL_TEXTURE_ENV_MODE (and read its
# encoding for all three modes at once).  sudo sh texenvdiff.sh
#   -> ~/RageProLT/gli_env{0,1,2}.txt
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
cc -o "$DIR/mach64_texenv" "$DIR/mach64_texenv.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
for cfg in 0 1 2; do
  echo "--- gli dump cfg=$cfg ---"
  gdb -batch -x "$DIR/glidump-wide.gdb" --args "$DIR/mach64_texenv" $cfg > "$ROOT/gli_env$cfg.txt" 2>&1
  echo "  wrote gli_env$cfg.txt ($(grep -c 0x "$ROOT/gli_env$cfg.txt") lines)"
done
kextunload /tmp/$B; echo "--- done ---"
