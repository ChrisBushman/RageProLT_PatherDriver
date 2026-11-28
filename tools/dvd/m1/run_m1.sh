#!/bin/sh
# usage: run_m1.sh [extra env e.g. DVDSTUB_ALTSCAN=1]  - rig on, 8 s playback dumping 150 frames, rig off, fetch to ./m1/
S=/tmp/claude-1000/-var-home-sam/e290b1e9-db53-4c64-9912-e23769ccb0f0/scratchpad/m1
ssh MDD "~/m0_rig.sh on && rm -f ~/dvdstub.log ~/m1_frames.yuv && ~/rungui.sh /Users/sam/m1_out.txt 40 \"cd /Users/sam; $1 DVDSTUB_YUV=/Users/sam/m1_frames.yuv DVDSTUB_FRAMES=150 ./dvd_play_test /Volumes/TESTDVD/VIDEO_TS 8\" >/dev/null; ~/m0_rig.sh off" | tail -2
scp -q MDD:m1_frames.yuv $S/ && scp -q MDD:dvdstub.log $S/m1_log.txt
cd $S && ./cmp m1_frames.yuv ref.yuv ${2:-12}
