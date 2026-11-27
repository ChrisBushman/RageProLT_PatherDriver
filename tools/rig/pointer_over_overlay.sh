#!/bin/sh
# pointer_over_overlay.sh: switch the Rage display to 1024x768, show the overlay (ovl_flip, 640x480 buffers stretched over the scanout, 15 flips x 3 s) and walk the pointer across it
# (cursor_walk) so the user can judge whether the arrow is visible over the video.  Run in the GUI session (rungui.sh).  Needs ~/cgmode ~/cursor_walk ~/rage-p1/tools/ragegl/ovl_flip.
RID=`$HOME/cgmode | grep ' 640x480 ' | head -1 | sed 's/display id \([0-9]*\):.*/\1/'`
echo "rage display id $RID"
$HOME/cgmode $RID 1024 768 130 force > /tmp/pov_cgmode.out 2>&1 &
CG=$!
sleep 12
$HOME/rage-p1/tools/ragegl/ovl_flip 2 15 3000 > /tmp/pov_flip.out 2>&1 &
FL=$!
sleep 8
echo "overlay up, walking pointer"
for k in 1 2 3; do $HOME/cursor_walk > /tmp/pov_walk$k.out 2>&1; done
$HOME/cursor_walk stay > /tmp/pov_stay.out 2>&1
echo "pointer parked on the overlay"
wait $FL
echo "overlay ended"; tail -3 /tmp/pov_flip.out
kill $CG; sleep 5
$HOME/mach64_arrange rage >/dev/null 2>&1
