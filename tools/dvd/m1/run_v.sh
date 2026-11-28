#!/bin/sh
# usage: run_v.sh N [env]  - play ~/m1v/discN on the MDD with the rig on, dump decode-order frames, compare with refN
S=/tmp/claude-1000/-var-home-sam/e290b1e9-db53-4c64-9912-e23769ccb0f0/scratchpad/m1
V=$1; shift
ssh MDD "~/m0_rig.sh on && rm -f ~/dvdstub.log ~/mv$V.yuv && ~/rungui.sh /Users/sam/mv$V.out 60 \"cd /Users/sam; $* DVDSTUB_DUMPDEC=1 DVDSTUB_YUV=/Users/sam/mv$V.yuv DVDSTUB_FRAMES=150 ./dvd_play_test /Users/sam/m1v/disc$V/VIDEO_TS 9\" >/dev/null; ~/m0_rig.sh off" | tail -1
scp -q MDD:mv$V.yuv $S/mv$V.yuv && scp -q MDD:dvdstub.log $S/mv$V.log
cd $S && ./cmp2 mv$V.yuv v/ref$V.yuv 150 60
