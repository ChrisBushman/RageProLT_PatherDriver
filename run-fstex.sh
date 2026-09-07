#!/bin/sh
# run-fstex.sh -- fullscreen TEXTURED, but NO allocator, to isolate texture-heap vs allocator.
# Fullscreen + textured (NOT minimal) + NO MACH64_VRAM_ALLOC (so setup_target does NOT run the Apple
# Z/texheap allocator; texheap falls back).  Few quads.  Compare to run-fslight.sh (which worked):
#   panic here  -> the TEXTURE-HEAP path is the fullscreen culprit (fix the texheap placement/guard)
#   renders/garbage, no panic -> the ALLOCATOR (Z/texheap alloc) in cramped fullscreen VRAM is it
# Run AT THE CONSOLE:  sh ~/run-fstex.sh  [NPART] [NFRAMES]
set -e
cd ~/RageProLT
cc -O2 -o ~/flurrywin tools/mach64_flurrywin.c -framework Carbon -framework AGL -framework OpenGL
echo "built ~/flurrywin -- running FULLSCREEN TEXTURED (no allocator) ${1:-8} quads x ${2:-60} frames..."
MACH64_FS=1 MACH64_GLD_FSYNC=1 ~/flurrywin "${1:-8}" "${2:-60}"
echo "fstex exited."
