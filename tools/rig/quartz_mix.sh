#!/bin/sh
# quartz_mix.sh: a broader mix of real Quartz traffic on the Rage display, driven by AppleScript only (no mouse; own Finder window):
# view switches (icon/list/column), resizes, folder navigation, window moves.  Prints the 2D client's counters before/after (every Rage2D* property, Refused must stay 0).
c() { ioreg -l -w0 | grep -E "\"Rage2D[A-Za-z]*\" =" | sed 's/.*"Rage2D//; s/" = /=/' | sort | tr '\n' ' '; }
echo "before: `c`"
osascript <<'AS' 2>&1 | head -5
tell application "Finder"
  make new Finder window to folder "Applications" of startup disk
  set bounds of front window to {20, 40, 560, 420}
  repeat 3 times
    set current view of front window to icon view
    delay 0.4
    set current view of front window to list view
    delay 0.4
    set current view of front window to column view
    delay 0.4
  end repeat
  repeat 6 times
    set bounds of front window to {20, 40, 400, 300}
    delay 0.2
    set bounds of front window to {20, 40, 600, 440}
    delay 0.2
  end repeat
  set target of front window to folder "Utilities" of folder "Applications" of startup disk
  delay 1
  set target of front window to home
  delay 1
  close front window
end tell
AS
echo "after:  `c`"
