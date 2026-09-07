#!/bin/sh
# run-fsprobe.sh -- build + run the SAFE fullscreen attach probe on the Lombard.
# Triggers gldAttachDrawable(type 0x5a) for a fullscreen AGL context, logs the descriptor + surface
# bind + rt-peek, then releases -- NO rendering (no glClear/draw/swap), so nothing writes geometry.
# Captures the display ~1s (brief black screen), then exits.  MACH64_GLD_FSYNC=1 so the log survives
# even if the attach hangs.  Run AT THE CONSOLE (fullscreen needs it):  sh run-fsprobe.sh
set -e
cd ~/RageProLT
cc -O2 -o ~/fsprobe tools/mach64_fsprobe.c -framework Carbon -framework AGL -framework OpenGL
echo "built ~/fsprobe -- running (attach only, no render, fsync log on)..."
MACH64_GLD_FSYNC=1 ~/fsprobe
echo "fsprobe exited.  read ~/mach64gld.log"
