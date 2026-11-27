#!/bin/sh
# wc_ab_all.sh : alternate wc 0,1,0,1 (each: restart session + 3 benchmark runs), results in ~/wc_ab.log
rm -f $HOME/wc_ab.log
for w in 0 1 0 1; do sh $HOME/rage-p1/tools/rig/wc_ab.sh $w; done
echo DONE >> $HOME/wc_ab.log
