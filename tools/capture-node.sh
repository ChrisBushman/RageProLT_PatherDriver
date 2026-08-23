#!/bin/sh
# Capture the framework's pixel-format config-node format from a SOFTWARE
# renderer via gdb (learn the exact 0x34-byte layout to replicate).  Removes our
# incomplete bundle first so the software path is used cleanly.
#   sudo sh capture-node.sh
DIR=`cd \`dirname "$0"\` && pwd`
echo "--- removing our incomplete GL bundle (so software renderer is used) ---"
rm -rf /System/Library/Extensions/ATIRageProGLDriver.bundle 2>/dev/null
echo "--- gdb capture: software renderer config nodes ---"
gdb -batch -x "$DIR/capture-node.gdb" "$DIR/mach64_gltest" 2>&1
echo "--- done ---"
