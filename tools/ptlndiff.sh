#!/bin/sh
# Dump the gli for 2 point-size/line-width configs to locate their gli offsets.
#   sudo sh ptlndiff.sh   -> ~/RageProLT/gli_pl{0,1}.txt
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
cc -o "$DIR/mach64_ptlnstate" "$DIR/mach64_ptlnstate.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
for cfg in 0 1; do
  echo "--- gli dump cfg=$cfg ---"
  gdb -batch -x "$DIR/glidump.gdb" --args "$DIR/mach64_ptlnstate" $cfg > "$ROOT/gli_pl$cfg.txt" 2>&1
  echo "  wrote gli_pl$cfg.txt"
done
kextunload /tmp/$B; echo "--- done ---"
