#!/bin/sh
cd $HOME/atlantis-win || exit 1
export MACH64_GLD_APPEND=1
GLUTSHIM_SECS=30 GLUTSHIM_POS="0,60" ./atlantis > out.o0.txt 2>&1 &
P1=$!
sleep 2
GLUTSHIM_SECS=10 GLUTSHIM_POS="150,100" ./atlantis > out.o1.txt 2>&1 &
P2=$!
echo "overlap pids: $P1 $P2"
wait $P2; echo front-closed
wait $P1; echo done
