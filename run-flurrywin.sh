#!/bin/sh
# run-flurrywin.sh -- build + run the Flurry-like windowed stress test on the Lombard.
# Normal 800x600 window, NPART colour textured quads/frame (Flurry-scale, known colours).
#
#   sh run-flurrywin.sh              # gentle default: 40 quads, 150 frames
#   sh run-flurrywin.sh 120 200      # heavier: 120 quads, 200 frames
#
# MACH64_GLD_FSYNC=1 forces each log line to disk, so if the engine wedges and you must hard-boot,
# ~/mach64gld.log still holds the last ops (last time it was lost because fsync was off).
set -e
cd ~/RageProLT
cc -O2 -o ~/flurrywin tools/mach64_flurrywin.c -framework Carbon -framework AGL -framework OpenGL
echo "built ~/flurrywin -- running ${1:-40} quads x ${2:-150} frames (fsync log on)..."
MACH64_VRAM_ALLOC=1 MACH64_GLD_FSYNC=1 ~/flurrywin "$@"
echo "flurrywin exited cleanly."
