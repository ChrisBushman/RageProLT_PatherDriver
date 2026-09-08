#!/bin/sh
# run-agpprobe.sh -- FULLSCREEN AGP-texture PROBE (v7 kext).  Confirms the new op=3 path end-to-end
# WITHOUT any sampling risk.  Runs the MINIMAL fullscreen render (flat, untextured -- the HW-proven
# stable path) but with MACH64_AGP_PROBE=1, so setup_target additionally allocates ONE 64x64 texture
# through the GART aperture (sel 10 op=3 -> new_agp_texture@0xf64c + memcpy + addToGART@0x4398) and
# logs the returned aperture offset.  It does NOT program any texture register, so the frame stays
# the stable untextured path -- no panic from a mis-mapped sample.
#
# NEEDS the v7 kext installed (sudo sh install-kext-v7.sh; shut down + power on) AND the driver
# rebuilt (this runs install-pathb.sh).  Run AT THE CONSOLE:  sh ~/run-agpprobe.sh  [NPART] [NFRAMES]
#
# Read ~/mach64gld.log afterwards -- the go/no-go lines are:
#   [agp] -> new_agp_texture size=16384 (sel 10 op=3)...     (last line if the kext op=3 panics)
#   [agp] <- kr=0x0 out=[gart=0x.. token=0x.. handle=0x..]   (the call returned -- no panic!)
#   [agp] PROBE gart_off=0x.. ... (APERTURE range >8MB ...)  (gart_off ready to use as TEX_0_OFF)
set -e
echo "== [1/2] rebuild + install the driver bundle (needs sudo) =="
sudo sh ~/RageProLT/install-pathb.sh
echo ""
echo "== [2/2] build + run the fullscreen AGP probe (flat render + op=3 alloc, no sampling) =="
cd ~/RageProLT
cc -O2 -o ~/flurrywin tools/mach64_flurrywin.c -framework Carbon -framework AGL -framework OpenGL
echo "built ~/flurrywin -- running FULLSCREEN + AGP PROBE ${1:-8} flat x ${2:-60} frames..."
MACH64_FS=1 MACH64_MIN=1 MACH64_AGP_PROBE=1 MACH64_GLD_FSYNC=1 ~/flurrywin "${1:-8}" "${2:-60}"
echo "agpprobe exited.  read ~/mach64gld.log  (grep '\[agp\]')"
