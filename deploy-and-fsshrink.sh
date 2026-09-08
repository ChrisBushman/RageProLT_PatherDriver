#!/bin/sh
# deploy-and-fsshrink.sh -- BUILD A of the shrink-surface fullscreen texheap pivot.
# Rebuilds the driver bundle, then runs a FLAT fullscreen render with MACH64_FS_SHRINK=1: the driver
# renders into an 800x600 backing (set_scale shrinks surf[0x140]/[0x142] -> shape_surface reallocs the
# color buffer) and presents it SCALED to the native display via surface_flush/build_swap.  Texheap is
# still OFF (flat) -- this only confirms set_scale doesn't panic, reveals the freed VRAM hole, and shows
# whether the present scales-to-fill or letterboxes.  DRIVER-ONLY (no kext change / no reboot).
#
# Run AT THE CONSOLE:  sh ~/deploy-and-fsshrink.sh  [NPART] [NFRAMES]
# Afterwards read ~/mach64gld.log -- the key lines are the [fs] before/after set_scale (backing
# off/pitch + surf[0x140:142] + scale-active[0x158]) and whether the screen fills or letterboxes.
set -e
echo "== [1/2] rebuild + install the driver bundle (needs sudo) =="
sudo sh ~/RageProLT/install-pathb.sh
echo ""
echo "== [2/2] build + run FLAT fullscreen + SHRINK (set_scale 800x600 -> scaled present) =="
cd ~/RageProLT
cc -O2 -o ~/flurrywin tools/mach64_flurrywin.c -framework Carbon -framework AGL -framework OpenGL
echo "built ~/flurrywin -- running FLAT FULLSCREEN + SHRINK ${1:-8} x ${2:-60} frames..."
MACH64_FS=1 MACH64_MIN=1 MACH64_FS_SHRINK=1 MACH64_GLD_FSYNC=1 ~/flurrywin "${1:-8}" "${2:-60}"
echo "fsshrink exited.  read ~/mach64gld.log  (grep '\[fs\]')"
