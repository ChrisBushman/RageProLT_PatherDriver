#!/bin/sh
# usage: run_show.sh N [secs] [cpu]  -> play ~/m1vN on the MDD with the rig on and the frames SHOWN through the hardware overlay (no dumps); cpu = CPU reconstruction + upload
V=$1; T=${2:-20}
ssh MDD "~/m0_rig.sh on && rm -f ~/dvdstub.log && ~/rungui.sh /Users/sam/show$V.out $(($T+50)) \"cd /Users/sam; ${3:+DVDSTUB_CPU=1} DVDSTUB_TS=1 ./dvd_play_test /Users/sam/m1v/disc$V/VIDEO_TS $T\" >/dev/null; ~/m0_rig.sh off" | tail -1
scp -q MDD:dvdstub.log /tmp/claude-1000/-var-home-sam/e290b1e9-db53-4c64-9912-e23769ccb0f0/scratchpad/m1/sz/show$V.log
grep -E "show failed|OVL|reconstruction mode|ShowMPBuffer" /tmp/claude-1000/-var-home-sam/e290b1e9-db53-4c64-9912-e23769ccb0f0/scratchpad/m1/sz/show$V.log | sort | uniq -c | sort -rn | head -5
