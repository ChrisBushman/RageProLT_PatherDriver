#!/bin/sh
# usage: stress.sh <tag> <vdma 0|1> <iterations>
cd ~/RageProLT/tools
i=0
while [ $i -lt $3 ]; do
  ~/stubctl notify >/dev/null
  if [ $2 = 1 ]; then export MACH64_VDMA=1; else unset MACH64_VDMA; fi
  echo "$i heavy" >> ~/stress_$1.count; sync
  PB_HEAVY=1500 ./mach64_pathb_suite heavy > ~/stress_$1.out 2>&1
  echo "$i multictx" >> ~/stress_$1.count; sync
  ~/stubctl notify >/dev/null
  ./mach64_pathb_suite multictx tex_mip > ~/stress_$1.out2 2>&1
  i=$((i+1))
done
echo done >> ~/stress_$1.count
