#!/bin/sh
# deploy-and-fstexshrink.sh -- BUILD B of the shrink-surface fullscreen texheap pivot: TEXTURED.
# Rebuilds the driver bundle, then runs a TEXTURED fullscreen render with MACH64_FS_SHRINK=1: render
# into an 800x600 backing (frees VRAM), place the texheap via Apple's mutex-locked best-fit heap in the
# freed hole (validated not to overlap the color backing), upload texels via the PROVEN op0 path, and
# present scaled to native.  Depth (Z) is ALSO allocated from the locked heap now (validated).
# DRIVER-ONLY (no reboot).
#
# Run AT THE CONSOLE:  sh ~/deploy-and-fstexshrink.sh  [NPART] [NFRAMES]
# Afterwards send the log lines:  grep -E '\[fs\]|texheap|z-buffer' ~/mach64gld.log
# and SAY whether the squares are TEXTURED (patterned) or flat-colored, and whether it's stable.
set -e
echo "== [1/2] rebuild + install the driver bundle (needs sudo) =="
sudo sh ~/RageProLT/install-pathb.sh
echo ""
echo "== [2/2] build + run TEXTURED fullscreen + SHRINK =="
cd ~/RageProLT
cc -O2 -o ~/flurrywin tools/mach64_flurrywin.c -framework Carbon -framework AGL -framework OpenGL
echo "built ~/flurrywin -- running TEXTURED FULLSCREEN + SHRINK ${1:-8} x ${2:-60} frames..."
MACH64_FS=1 MACH64_FS_SHRINK=1 MACH64_GLD_FSYNC=1 ~/flurrywin "${1:-8}" "${2:-60}"
echo "fstexshrink exited.  read ~/mach64gld.log"
