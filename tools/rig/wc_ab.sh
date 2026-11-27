#!/bin/sh
# wc_ab.sh 0|1 : set the stub's write-combine flag, restart the GUI session (kill a stale ws_patcher first: it blocks the WindowServer restart), arrange the displays, run wc_bench 3 times.
W=$1
$HOME/stubctl_wc wc $W >/dev/null
for p in `ps auxww | grep '[w]s_patcher' | awk '{print $2}'`; do sudo kill $p; done
sudo killall loginwindow; sleep 35
$HOME/mach64_arrange rage >/dev/null 2>&1
LOG=$HOME/wc_ab.log
echo "== wc=$W" >> $LOG
for k in 1 2 3; do $HOME/rungui.sh /tmp/wcb.out 90 "sh $HOME/rage-p1/tools/rig/wc_bench.sh 20" >/dev/null 2>&1; tr "\n" " " < /tmp/wcb.out >> $LOG; echo >> $LOG; done
