#!/bin/sh
# Install bundle + kext, run the texture app under gdb, trace where GLEngine
# sources GL_MAX_TEXTURE_SIZE (read-site of the 0 that rejects glTexImage2D).
#   sudo sh texcaptest.sh    (output saved to ~/RageProLT/texcap-output.rtf-ish txt)
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
GLB="$ROOT/gldriver/ATIRageProGLDriver.bundle"
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
echo "--- installing bundle + kext ---"
rm -rf "$DST"; cp -R "$GLB" "$DST"; chown -R root:wheel "$DST"
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log
cc -o "$DIR/mach64_texapp" "$DIR/mach64_texapp.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 || exit 1
echo "--- gdb: glGetIntegerv(GL_MAX_TEXTURE_SIZE) read-site trace ---"
gdb -batch -x "$DIR/texcap.gdb" "$DIR/mach64_texapp" 2>&1 | tee "$ROOT/texcap-output.txt"
echo "--- unloading kext ---"; kextunload /tmp/$B
echo "--- done (full trace in ~/RageProLT/texcap-output.txt) ---"
