#!/bin/sh
# Process start/stop stress for the Path B driver on the headless rig (run as root on the G4 after bringup.sh).
#   usage: sudo sh soak-cycles.sh <seconds to wait after notify> <cycles> <frames per cycle>   e.g.  3 60 1500
# Each cycle: one `stubctl notify`, wait, then a fresh `mach64_pathb_suite soak` process. Progress is appended AND synced to
# ~/cyc.out after every step, so after a kernel panic the file shows which cycle/step was running. MACH64_GLD_FSYNC makes the
# plugin log survive a panic as well. Set STUBCTL / TOOLS if they are elsewhere.
TOOLS=${TOOLS:-/Users/sam/RageProLT/tools}; STUBCTL=${STUBCTL:-/Users/sam/stubctl}; OUT=${OUT:-/Users/sam/cyc.out}
cd "$TOOLS" || exit 1
export HOME=/Users/sam MACH64_GLD_FSYNC=1
i=0
while [ $i -lt $2 ]; do
  echo "cycle $i: notify" >> $OUT; sync
  $STUBCTL notify >/dev/null 2>&1; echo "cycle $i: notified, sleeping $1" >> $OUT; sync; sleep $1
  echo "cycle $i: running" >> $OUT; sync
  env PB_SOAK=$3 ./mach64_pathb_suite soak 2>&1 | grep -E "SUITE|attach" >> $OUT; sync
  i=$((i+1))
done
echo DONE >> $OUT; sync
