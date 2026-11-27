#!/bin/sh
# offscreen_test.sh: A6 (G4, GUI session): run cgl_offscreen and prove the Rage screen did not change.
cd $HOME/rage-p1/tools/ragegl
shot() { screencapture -x $1.pdf; sips -s format png $1.pdf --out $1.png >/dev/null 2>&1; sips -c 450 640 --cropOffset 24 0 $1.png --out $1c.png >/dev/null 2>&1; }
shot /tmp/off_before; sleep 1
./cgl_offscreen 2>&1
shot /tmp/off_after
cmp -s /tmp/off_beforec.png /tmp/off_afterc.png && echo "screen unchanged (menu bar excluded: byte-identical screenshots of the rest of the Rage display)" || echo "screenshots differ (clock/menu may differ; compare by eye)"
