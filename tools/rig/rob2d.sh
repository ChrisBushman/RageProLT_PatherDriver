#!/bin/sh
# rob2d.sh: 2D-client robustness on the G4.  R1: kill -9 a process in the middle of a 2D stream, 5 times.  R2: mode changes while a 2D stream runs.  Checks: no panic, no leaked 2D clients, twod_test passes afterwards.
cd $HOME/rage-p1/tools/ragegl
n() { ioreg -l -w0 | grep -v IOKitDiagnostics | grep -c "<class RageLT2DClient"; }
echo "2D clients before: `n`"
i=0; while [ $i -lt 5 ]; do ./twod_bench >/dev/null 2>&1 & P=$!; sleep 1.5; kill -9 $P; sleep 1; i=`expr $i + 1`; done
echo "R1 after 5 kill -9: 2D clients `n` (WindowServer's is 1)"
( while :; do ./twod_bench >/dev/null 2>&1; done ) & LOOP=$!
sleep 2
for m in "800 600 16" "640 480 32" "800 600 32" "640 480 16"; do
  set -- $m
  ( cd $HOME/stub-modes && ./cgmode 1535231426 $1 $2 3000 force $3 > cgR.out 2>&1 & echo $! > /tmp/cg.pid )
  sleep 9
  echo "R2 mode $m: `head -1 $HOME/stub-modes/cgR.out | cut -c1-90`; panics `grep -c Unresolved /Library/Logs/panic.log 2>/dev/null`"
  kill `cat /tmp/cg.pid` 2>/dev/null; for p in `ps auxww | grep "[c]gmode 1535231426" | grep -v "bash -c" | awk '{print $2}'`; do kill $p; done
  sleep 4
done
kill $LOOP; for p in `ps auxww | grep "[t]wod_bench" | awk '{print $2}'`; do kill -9 $p; done; sleep 2
sh $HOME/rage-p1/tools/rig/run2d.sh 640 480 16 2>&1
echo "2D clients after: `n`"
