#!/bin/sh
# usage: run2d.sh W H BITS : hold the Rage at WxH BITS bpp, run twod_test, release
cd ~/stub-modes && (nohup ./cgmode 1535231426 $1 $2 3000 force $3 > cgT.out 2>&1 &)
sleep 15
cd ~/rage-p1/tools/ragegl && ./twod_test 2>&1 | grep -E "display|FAIL|checks|line|colour"
for p in `ps auxww | grep "[c]gmode 1535231426" | grep -v "bash -c" | awk '{print $2}'`; do kill $p; done
