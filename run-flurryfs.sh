#!/bin/sh
# run-flurryfs.sh -- build + run the Flurry-like stress test FULLSCREEN on the Lombard.
# MACH64_FS=1 -> aglSetFullScreen(1024x768).  MACH64_VRAM_ALLOC=1 -> collision-free fullscreen Z
# (the guessed Z gap lands past 8MB VRAM at fullscreen size).  Bounded frames -> auto-exits.
#
#   sh run-flurryfs.sh            # 60 quads x 120 frames fullscreen (~4s)
#   sh run-flurryfs.sh 120 200
#
# Run AT THE CONSOLE.  It captures the whole display for a few seconds then releases + exits on its
# own.  If it ever sticks, kill it over ssh from another machine:  ssh lombard killall -9 flurrywin
set -e
cd ~/RageProLT
cc -O2 -o ~/flurrywin tools/mach64_flurrywin.c -framework Carbon -framework AGL -framework OpenGL
echo "built ~/flurrywin -- running FULLSCREEN ${1:-60} quads x ${2:-120} frames..."
MACH64_FS=1 MACH64_VRAM_ALLOC=1 MACH64_GLD_FSYNC=1 ~/flurrywin "${1:-60}" "${2:-120}"
echo "flurryfs exited."
