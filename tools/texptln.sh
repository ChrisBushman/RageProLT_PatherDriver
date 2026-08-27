#!/bin/sh
# Verify TEXTURED points/lines (per-vertex texcoords).  Default build.  Texture encodes its
# coords (R=s, G=t).  PASS = the 4 points are 4 DISTINCT solid colours (blue/magenta/green/
# yellow, R growing left->right) and the 3 lines are smooth colour GRADIENTS.  FAIL(old
# behaviour) = everything one flat colour = texel(0,0) sampled uniformly.
# Run on the Lombard under sudo:  sudo sh tools/texptln.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; make bundle ) || exit 1
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_texptln" "$DIR/mach64_texptln.c" \
    -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
"$DIR/mach64_texptln"
kextunload /tmp/$B
cp -f /tmp/mach64gld.log "$ROOT/texptln-log.txt" 2>/dev/null
echo "=== screenshot: 4 distinct-colour points + 3 gradient lines (not all one flat colour) ==="
echo "--- done ---"
