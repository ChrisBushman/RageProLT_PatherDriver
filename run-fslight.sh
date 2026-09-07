#!/bin/sh
# run-fslight.sh -- MINIMAL fullscreen render test to isolate the fullscreen panic.
# Fullscreen (MACH64_FS) + MINIMAL (MACH64_MIN: no texture, no blend -> no texture-heap upload) +
# NO allocator (no MACH64_VRAM_ALLOC -> Z skipped by the bounds-guard) + few flat triangles.
# The bundle is unchanged from the fsprobe deploy (probe + display-associated bail), so this only
# rebuilds the test tool.  Run AT THE CONSOLE:  sh ~/run-fslight.sh  [NPART] [NFRAMES]
#
# If this renders fullscreen with NO panic -> the panic was the texture-heap/allocator in cramped
# fullscreen VRAM (fixable).  If it STILL panics -> the geometry submit/present itself is the cause.
set -e
cd ~/RageProLT
cc -O2 -o ~/flurrywin tools/mach64_flurrywin.c -framework Carbon -framework AGL -framework OpenGL
echo "built ~/flurrywin -- running MINIMAL FULLSCREEN ${1:-8} flat tris-quads x ${2:-60} frames..."
MACH64_FS=1 MACH64_MIN=1 MACH64_GLD_FSYNC=1 ~/flurrywin "${1:-8}" "${2:-60}"
echo "fslight exited."
