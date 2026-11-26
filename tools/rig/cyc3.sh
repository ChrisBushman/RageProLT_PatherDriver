#!/bin/sh
cd /Users/sam/RageProLT/tools
export HOME=/Users/sam MACH64_GLD_FSYNC=1 PB_NOREGS=1
i=0
while [ $i -lt $2 ]; do
  echo "cycle $i: notify" >> /Users/sam/cyc.out; sync
  /Users/sam/stubctl notify >/dev/null 2>&1; echo "cycle $i: notified, sleeping $1" >> /Users/sam/cyc.out; sync; sleep $1
  echo "cycle $i: running" >> /Users/sam/cyc.out; sync
  env PB_SOAK=$3 ./mach64_pathb_suite soak 2>&1 | grep -E "SUITE|attach" >> /Users/sam/cyc.out; sync
  i=$((i+1))
done
echo DONE >> /Users/sam/cyc.out; sync
