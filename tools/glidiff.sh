#!/bin/sh
# Capture the gli context in BOTH configs for diffing:
#  1) HARDWARE: our bundle installed, run mach64_fstest  -> ~/RageProLT/gli_hw.txt
#  2) SOFTWARE: bundle removed,       run mach64_swtest  -> ~/RageProLT/gli_sw.txt
# Both break GLEngine sub_4530 and dump gli[0..0x8000].
#   sudo sh glidiff.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
OUT="$ROOT"

echo "=== [1/2] HARDWARE gli dump ==="
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
cc -o "$DIR/mach64_fstest" "$DIR/mach64_fstest.c" -framework Carbon -framework AGL -framework OpenGL 2>/dev/null
gdb -batch -x "$DIR/glidump.gdb" "$DIR/mach64_fstest" > "$OUT/gli_hw.txt" 2>&1
kextunload /tmp/$B 2>/dev/null
echo "wrote gli_hw.txt ($(grep -c 0x "$OUT/gli_hw.txt") lines)"

echo "=== [2/2] SOFTWARE gli dump ==="
rm -rf "$DST"
cc -o "$DIR/mach64_swtest" "$DIR/mach64_swtest.c" -framework Carbon -framework AGL -framework OpenGL 2>/dev/null
gdb -batch -x "$DIR/glidump.gdb" "$DIR/mach64_swtest" > "$OUT/gli_sw.txt" 2>&1
echo "wrote gli_sw.txt ($(grep -c 0x "$OUT/gli_sw.txt") lines)"
echo "=== done -- fetch gli_hw.txt + gli_sw.txt ==="
