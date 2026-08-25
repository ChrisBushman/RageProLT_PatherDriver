#!/bin/sh
# Dump the gli for GL_BLEND off(0) / on+srcalpha(1) / on+additive(2), to locate the
# blend-enable bit and blend-func fields.  sudo sh blenddiff.sh
#   -> ~/RageProLT/gli_bl{0,1,2}.txt
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
cc -o "$DIR/mach64_blendstate" "$DIR/mach64_blendstate.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
for cfg in 0 1 2; do
  echo "--- gli dump cfg=$cfg ---"
  gdb -batch -x "$DIR/glidump-wide.gdb" --args "$DIR/mach64_blendstate" $cfg > "$ROOT/gli_bl$cfg.txt" 2>&1
  echo "  wrote gli_bl$cfg.txt ($(grep -c 0x "$ROOT/gli_bl$cfg.txt") lines)"
done
kextunload /tmp/$B; echo "--- done ---"
