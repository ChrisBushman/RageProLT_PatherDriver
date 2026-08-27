#!/bin/sh
# "Wider find" diagnostic: enumerate the surfaces Apple's ATIRagePro accelerator has registered,
# to see what find_surface_for_id actually runs against -- and whether our own set_id_mode
# registration shows up in the GL context's view.  Talks DIRECTLY to Apple's already-loaded
# ATIRagePro.kext via IOKit; does NOT load our kext/bundle and touches nothing else.  Cold-safe
# (get_surface_info is read-only, misses return 0x2c2, no panic path).  Run on the Lombard:
#   sudo sh tools/surfscan.sh            # registers idx 0x13 by default
#   sudo sh tools/surfscan.sh 0x1b       # or a specific idx
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`

cc -O2 -o "$DIR/mach64_surfscan" "$DIR/mach64_surfscan.c" -framework IOKit 2>&1 \
    || { echo "BUILD FAILED"; exit 1; }

echo "=== running surfscan (idx=${1:-0x13}) ==="
"$DIR/mach64_surfscan" ${1:+"$1"} 2>&1 | tee "$ROOT/surfscan-out.txt"
echo "--- done; output in surfscan-out.txt ---"
