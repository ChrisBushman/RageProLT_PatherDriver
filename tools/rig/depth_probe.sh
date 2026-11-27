#!/bin/sh
# depth_probe.sh BITS: hold the Rage at 640x480 BITS bpp, then run twod_test and gl_suite (and a plain display query), release.  Used for D5 (8 bpp / other depths on the stub).
B=$1
cd $HOME/stub-modes && (nohup ./cgmode 1535231426 640 480 3000 force $B > cgD.out 2>&1 &)
sleep 15
cd $HOME/rage-p1/tools/ragegl
echo "== $B bpp: cgmode: `head -1 $HOME/stub-modes/cgD.out | cut -c1-100`"
$HOME/rage-p1/tools/rig/chipinfo | cut -d'|' -f1 | head -2
./twod_test 2>&1 | grep -E "display|FAIL|refus" | head -5
./gl_suite 2>&1 | grep -E "SUITE|FAIL" | head -6
for p in `ps auxww | grep "[c]gmode 1535231426" | grep -v "bash -c" | awk '{print $2}'`; do kill $p; done
sleep 4
