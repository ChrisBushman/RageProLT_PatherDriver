#!/bin/sh
# wc_bench.sh N: WindowServer CPU time for N full-screen repaints of the Rage display (mach64_repaint = a plain window opened and closed over the whole display).
# Run inside the GUI session (rungui.sh) with ~/mach64_repaint built from tools/mach64_repaint.c.  Prints WindowServer cumulative CPU time before/after.
N=${1:-20}
WS=`ps auxww | grep '[W]indowServer' | grep -v ws_patcher | awk '{print $2}' | head -1`
cpu() { ps -o time= -p $WS; }
A=`cpu`; i=0
while [ $i -lt $N ]; do $HOME/mach64_repaint >/dev/null 2>&1; i=`expr $i + 1`; done
B=`cpu`; echo "WindowServer cpu: $A -> $B for $N repaints"
