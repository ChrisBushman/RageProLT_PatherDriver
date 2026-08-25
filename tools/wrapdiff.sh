#!/bin/sh
# Probe where GL_TEXTURE_WRAP_S/T lives: run REPEAT(0) / CLAMP(1) / CLAMP-S-only(2)
# and diff the driver's state-object + texobj header dumps.  sudo sh wrapdiff.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"; DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
cc -o "$DIR/mach64_wrapstate" "$DIR/mach64_wrapstate.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
for cfg in 0 1 2; do
  rm -f /tmp/mach64gld.log
  "$DIR/mach64_wrapstate" $cfg >/dev/null 2>&1
  grep -aE 'sobj\+|tobj\+' /tmp/mach64gld.log > "$ROOT/wrap$cfg.txt" 2>/dev/null
  echo "cfg=$cfg -> wrap$cfg.txt ($(wc -l < "$ROOT/wrap$cfg.txt") lines)"
done
echo "=== diff REPEAT(0) vs CLAMP(1) ==="
diff "$ROOT/wrap0.txt" "$ROOT/wrap1.txt"
echo "=== diff CLAMP(1) vs CLAMP-S-only(2) ==="
diff "$ROOT/wrap1.txt" "$ROOT/wrap2.txt"
kextunload /tmp/$B; echo "--- done ---"
