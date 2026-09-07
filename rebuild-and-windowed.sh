#!/bin/sh
# rebuild-and-windowed.sh -- rebuild the driver bundle, then run the WINDOWED agltexbig regression test.
# Confirms the "disable legacy target path" change didn't break windowed rendering (safe: windowed
# does not panic).  Run as the normal user:  sh ~/rebuild-and-windowed.sh
set -e
echo "== rebuilding + installing the driver bundle (needs sudo) =="
sudo sh ~/RageProLT/install-pathb.sh
echo ""
echo "== running WINDOWED agltexbig (800x600 textured+depth) =="
MACH64_VRAM_ALLOC=1 MACH64_GLD_FSYNC=1 ~/agltexbig
echo "agltexbig exited."
