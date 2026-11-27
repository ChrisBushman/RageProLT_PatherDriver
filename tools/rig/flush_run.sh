#!/bin/sh
# flush_run.sh N: WindowServer CPU time and wall time for N frames of flush_bench (GUI session).
N=${1:-1000}
WS=`ps auxww | grep '[W]indowServer' | grep -v ws_patcher | awk '{print $2}' | head -1`
A=`ps -o time= -p $WS`; T0=`date +%s`
$HOME/flush_bench $N
B=`ps -o time= -p $WS`; T1=`date +%s`
echo "WindowServer cpu: $A -> $B, wall `expr $T1 - $T0` s, $N frames"
