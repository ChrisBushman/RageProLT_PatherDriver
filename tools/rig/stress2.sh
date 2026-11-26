#!/bin/sh
cd ~/RageProLT/tools
~/stubctl notify >/dev/null
export MACH64_VDMA=1
i=0
while [ $i -lt $1 ]; do
  echo "$i" >> ~/stress_e.count; sync
  PB_HEAVY=1500 ./mach64_pathb_suite heavy > ~/stress_e.out 2>&1
  grep -E "missing|attach" ~/stress_e.out | cut -c1-70 >> ~/stress_e.res
  ./mach64_pathb_suite multictx tex_mip > ~/stress_e.out2 2>&1
  grep -E "^SUITE" ~/stress_e.out2 >> ~/stress_e.res2
  i=$((i+1))
done
echo done >> ~/stress_e.count
