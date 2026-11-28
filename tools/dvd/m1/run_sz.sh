#!/bin/sh
# usage: run_sz.N W H  -> run_sz.sh N W H : play ~/m1vN on the MDD with the rig on (stub bundle), dump decode-order frames, compare to sz/refN.yuv
S=/tmp/claude-1000/-var-home-sam/e290b1e9-db53-4c64-9912-e23769ccb0f0/scratchpad/m1
V=$1; W=$2; H=$3
ssh MDD "~/m0_rig.sh on && rm -f ~/dvdstub.log ~/mv$V.yuv && ~/rungui.sh /Users/sam/mv$V.out $((${PLAY:-9}+50)) \"cd /Users/sam; ${CPU:+DVDSTUB_CPU=$CPU} ${BOTH:+DVDSTUB_BOTH=1} ${ESCALL:+DVDSTUB_ESCALL=1} DVDSTUB_TS=1 DVDSTUB_DUMPDEC=1 DVDSTUB_YUV=/Users/sam/mv$V.yuv DVDSTUB_FRAMES=150 ./dvd_play_test /Users/sam/m1v/disc$V/VIDEO_TS ${PLAY:-9}\" >/dev/null; ~/m0_rig.sh off" | tail -1
scp -q MDD:mv$V.yuv $S/sz/mv$V.yuv && scp -q MDD:dvdstub.log $S/sz/mv$V.log
cd $S && ./cmp2_${W}x${H} sz/mv$V.yuv sz/ref$V.yuv 150 60
