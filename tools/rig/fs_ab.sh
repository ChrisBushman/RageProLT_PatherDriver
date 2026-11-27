#!/bin/sh
# fs_ab.sh 0|1 [runs]: set the stub's write-combine flag, restart the GUI session, arrange displays, run flushscroll.sh `runs` times (default 3); results appended to ~/fs_ab.log.
W=$1; R=${2:-3}
$HOME/stubctl_wc wc $W >/dev/null
for p in `ps auxww | grep '[w]s_patcher' | awk '{print $2}'`; do sudo kill $p; done
sudo killall loginwindow; sleep 35
$HOME/mach64_arrange rage >/dev/null 2>&1
LOG=$HOME/fs_ab.log
echo "== wc=$W" >> $LOG
k=0; while [ $k -lt $R ]; do k=`expr $k + 1`; $HOME/rungui.sh /tmp/fs.out 200 "sh $HOME/rage-p1/tools/rig/flushscroll.sh 150" >/dev/null 2>&1; tr "\n" " " < /tmp/fs.out >> $LOG; echo >> $LOG; done
