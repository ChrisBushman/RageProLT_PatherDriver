#!/bin/sh
# trymode.sh W H BITS : hold the Rage at WxH BITS bpp (Apple FIFO already switched on), read the FIFO back, start Atlantis for 120 s
W=$1; H=$2; B=$3
for p in `ps auxww | grep "[c]gmode 1535231426" | grep -v "bash -c" | awk '{print $2}'`; do kill $p; done
sleep 3
cd ~/stub-modes && (nohup ./cgmode 1535231426 $W $H 3000 force $B > cgT.out 2>&1 &)
sleep 15
./cgmode | head -1
~/rage-p1/tools/rig/chipinfo | cut -d'|' -f1
~/rungui.sh /tmp/atl.out 150 "cd \$HOME/atlantis-win && env RAGE_STATS=1 GLUTSHIM_SECS=120 GLUTSHIM_POS=0,60 ./atlantis" >/dev/null 2>&1 &
sleep 25
tail -1 /tmp/atl.out | cut -c1-120; grep -c Unresolved /Library/Logs/panic.log
